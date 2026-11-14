#include "BluetoothOutput.h"
#if FEATURE_BLUETOOTH
#include <cstring>
#include <Preferences.h>
#include <esp_gap_bt_api.h>
#include <esp_avrc_api.h>
#include <freertos/queue.h>

// A2DP library owns the Classic GAP callback. Intercept authentication through
// its VIRTUAL method rather than installing a second esp_bt_gap_register_callback.
class MonoPodA2DPSource : public BluetoothA2DPSource {
protected:
  void app_gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) override {
    if(BluetoothAudioOutput::onGap(event, param))return;
    BluetoothA2DPSource::app_gap_callback(event, param);
  }
};
static MonoPodA2DPSource btDevice;
static QueueHandle_t remoteQueue = nullptr;
static uint32_t lastInquiry = 0;
static uint32_t lastLabelSync = 0;
static bool hasAddress(const uint8_t a[6], const uint8_t b[6]) {return memcmp(a,b,6)==0;}

int16_t BluetoothAudioOutput::pcm[CAPACITY*2] = {};
volatile uint16_t BluetoothAudioOutput::rd=0, BluetoothAudioOutput::wr=0;
portMUX_TYPE BluetoothAudioOutput::mux=portMUX_INITIALIZER_UNLOCKED;
bool BluetoothAudioOutput::initialized=false;
uint8_t BluetoothAudioOutput::foundCount=0;
BTHeadset BluetoothAudioOutput::found[16] = {};
BTPairRequest BluetoothAudioOutput::request = {};

BluetoothAudioOutput::BluetoothAudioOutput(){hertz=44100;channels=2;SetGain(0.1f);}
bool BluetoothAudioOutput::ensureStarted(){
  if(initialized)return true;
  if(!remoteQueue)remoteQueue=xQueueCreate(12,sizeof(Key));
  if(!remoteQueue)return false;
  btDevice.set_data_callback(callback);
  btDevice.set_ssid_callback(discoverCallback);
  btDevice.set_avrc_passthru_command_callback(onAvrc);
  btDevice.set_on_connection_state_changed(onConnection);
  btDevice.set_auto_reconnect(false);
  btDevice.set_ssp_enabled(true);
  btDevice.start();
  initialized=true;
  MONOPOD_LOG.println("BT: stack started; waiting for explicit device selection");
  return true;
}
bool BluetoothAudioOutput::begin(){
  portENTER_CRITICAL(&mux);rd=wr=0;portEXIT_CRITICAL(&mux);
  return ensureStarted();
}
bool BluetoothAudioOutput::SetRate(int hz){if(hz!=44100)return false;hertz=hz;return true;}
bool BluetoothAudioOutput::SetChannels(int n){if(n!=1&&n!=2)return false;channels=n;return true;}
bool BluetoothAudioOutput::ConsumeSample(int16_t sample[2]){
  portENTER_CRITICAL(&mux);
  uint16_t next=(wr+1)%CAPACITY;
  if(next==rd){portEXIT_CRITICAL(&mux);return false;}
  pcm[wr*2]=Amplify(sample[0]);pcm[wr*2+1]=Amplify(channels==1?sample[0]:sample[1]);
  wr=next;portEXIT_CRITICAL(&mux);return true;
}
int32_t BluetoothAudioOutput::callback(uint8_t *out,int32_t bytes){
  if(!out||bytes<0)return 0;
  int32_t frames=bytes/4;
  int16_t *target=reinterpret_cast<int16_t*>(out);
  for(int32_t i=0;i<frames;i++){
    portENTER_CRITICAL(&mux);
    if(rd!=wr){target[2*i]=pcm[rd*2];target[2*i+1]=pcm[rd*2+1];rd=(rd+1)%CAPACITY;}
    else target[2*i]=target[2*i+1]=0;
    portEXIT_CRITICAL(&mux);
  }
  return frames*4;
}
bool BluetoothAudioOutput::stop(){
  portENTER_CRITICAL(&mux);rd=wr=0;portEXIT_CRITICAL(&mux);return true;
}
String BluetoothAudioOutput::addressString(const uint8_t a[6]){
  char str[18];snprintf(str,sizeof(str),"%02X:%02X:%02X:%02X:%02X:%02X",a[0],a[1],a[2],a[3],a[4],a[5]);
  return String(str);
}
static String addressKey(const uint8_t a[6]){
  char str[13];snprintf(str,sizeof(str),"%02X%02X%02X%02X%02X%02X",a[0],a[1],a[2],a[3],a[4],a[5]);
  return String(str);
}
bool BluetoothAudioOutput::discoverCallback(const char* name,esp_bd_addr_t addr,int rssi){
  if(!name||!name[0])return false;
  portENTER_CRITICAL(&mux);
  int pos=-1;
  for(int i=0;i<foundCount;i++)if(hasAddress(found[i].address,addr)){pos=i;break;}
  if(pos<0 && foundCount<16)pos=foundCount++;
  if(pos>=0){
    strncpy(found[pos].name,name,sizeof(found[pos].name)-1);
    found[pos].name[sizeof(found[pos].name)-1]=0;
    memcpy(found[pos].address,addr,6);
    found[pos].rssi=rssi;found[pos].seenMs=millis();
  }
  portEXIT_CRITICAL(&mux);
  return false; // Prevent automatic A2DP connection while browsing.
}
uint8_t BluetoothAudioOutput::deviceCount(){
  portENTER_CRITICAL(&mux);uint8_t n=foundCount;portEXIT_CRITICAL(&mux);return n;
}
BTHeadset BluetoothAudioOutput::discovered(uint8_t i){
  BTHeadset d;portENTER_CRITICAL(&mux);if(i<foundCount)d=found[i];portEXIT_CRITICAL(&mux);return d;
}
uint8_t BluetoothAudioOutput::bondCount(){return initialized?(uint8_t)min(32,esp_bt_gap_get_bond_device_num()):0;}
BTHeadset BluetoothAudioOutput::bonded(uint8_t i){
  BTHeadset r;if(!initialized)return r;
  int num=esp_bt_gap_get_bond_device_num();
  if(num<=0||i>=num||num>64)return r;
  esp_bd_addr_t *addresses=(esp_bd_addr_t*)calloc(num,sizeof(esp_bd_addr_t));
  if(!addresses)return r;
  if(esp_bt_gap_get_bond_device_list(&num,addresses)==ESP_OK && i<num){
    memcpy(r.address,addresses[i],6);
    // Names are cached in NVS on the foreground thread; bonds themselves
    // are stored by the Bluetooth stack, not in this application namespace.
    Preferences prefs;
    if(prefs.begin("bt-labels",true)){
      String label=prefs.getString(addressKey(r.address).c_str(),"");
      prefs.end();
      if(label.length())strlcpy(r.name,label.c_str(),sizeof(r.name));
    }
    if(!r.name[0])strlcpy(r.name,addressString(r.address).c_str(),sizeof(r.name));
  }
  free(addresses);return r;
}
void BluetoothAudioOutput::service(){
  if(!initialized || millis()-lastLabelSync<8000)return;
  lastLabelSync=millis();
  Preferences prefs;if(!prefs.begin("bt-labels",false))return;
  uint8_t n=deviceCount();
  for(uint8_t i=0;i<n;i++){
    BTHeadset d=discovered(i);
    if(!d.name[0])continue;
    String key=addressKey(d.address),old=prefs.getString(key.c_str(),"");
    if(old!=d.name)prefs.putString(key.c_str(),d.name);
  }
  prefs.end();
}
bool BluetoothAudioOutput::startScan(){
  if(!ensureStarted())return false;
  if(scanning())return true;
  // A2DP library owns GAP; using discovery functions doesn't replace callback.
  lastInquiry=millis();
  const esp_err_t e=esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY,8,0);
  MONOPOD_LOG.printf("BT: inquiry start -> %d\n",(int)e);
  return e==ESP_OK;
}
bool BluetoothAudioOutput::connectAddress(const uint8_t addr[6]){
  if(!ensureStarted()||!addr)return false;
  esp_bd_addr_t a;memcpy(a,addr,6);
  if(scanning())esp_bt_gap_cancel_discovery();
  MONOPOD_LOG.printf("BT: connect %s\n",addressString(addr).c_str());
  return btDevice.connect_to(a);
}
bool BluetoothAudioOutput::disconnect(){if(!initialized)return false;btDevice.disconnect();return true;}
bool BluetoothAudioOutput::removeBond(const uint8_t addr[6]){
  if(!initialized||!addr)return false;
  esp_bd_addr_t a;memcpy(a,addr,6);
  if(connectedTo(addr))disconnect();
  esp_err_t e=esp_bt_gap_remove_bond_device(a);
  if(e==ESP_OK){
    Preferences prefs;if(prefs.begin("bt-labels",false)){
      prefs.remove(addressKey(addr).c_str());prefs.end();
    }
  }
  MONOPOD_LOG.printf("BT: remove bond %s -> %d\n",addressString(addr).c_str(),(int)e);
  return e==ESP_OK;
}
bool BluetoothAudioOutput::connected(){return initialized&&btDevice.is_connected();}
bool BluetoothAudioOutput::connectedTo(const uint8_t addr[6]){
  if(!connected())return false;
  esp_bd_addr_t *last=btDevice.get_last_peer_address();
  return last&&memcmp(*last,addr,6)==0;
}
bool BluetoothAudioOutput::scanning(){return initialized&&btDevice.is_discovery_active();}
void BluetoothAudioOutput::onConnection(esp_a2d_connection_state_t state, void *ctx){
  (void)ctx;
  MONOPOD_LOG.printf("BT A2DP state=%d\n",(int)state);
}
void BluetoothAudioOutput::onAvrc(uint8_t cmd,bool released){
  if(!released||!remoteQueue)return; // one action per complete key, no double trigger
  Key k=Key::None;
  if(cmd==ESP_AVRC_PT_CMD_PLAY)k=Key::RemotePlay;
  else if(cmd==ESP_AVRC_PT_CMD_PAUSE||cmd==ESP_AVRC_PT_CMD_STOP)k=Key::RemotePause;
  else if(cmd==ESP_AVRC_PT_CMD_FORWARD)k=Key::Next;
  else if(cmd==ESP_AVRC_PT_CMD_BACKWARD)k=Key::Previous;
  if(k!=Key::None){
    MONOPOD_LOG.printf("BT AVRCP key=0x%02X release=%d\n",cmd,(int)released);
    xQueueSend(remoteQueue,&k,0);
  }
}
Key BluetoothAudioOutput::pollRemote(){
  Key k=Key::None;
  if(remoteQueue)xQueueReceive(remoteQueue,&k,0);
  return k;
}
bool BluetoothAudioOutput::onGap(esp_bt_gap_cb_event_t e,esp_bt_gap_cb_param_t *p){
  if(!p)return false;
  BTPairRequest r;
  switch(e){
    case ESP_BT_GAP_PIN_REQ_EVT:
      r.kind=BTPairKind::LegacyPin;memcpy(r.address,p->pin_req.bda,6);
      r.minDigits=p->pin_req.min_16_digit?16:4;r.maxDigits=16;break;
    case ESP_BT_GAP_CFM_REQ_EVT:
      r.kind=BTPairKind::ConfirmNumber;memcpy(r.address,p->cfm_req.bda,6);
      r.number=p->cfm_req.num_val;break;
    case ESP_BT_GAP_KEY_REQ_EVT:
      r.kind=BTPairKind::Passkey;memcpy(r.address,p->key_req.bda,6);
      r.minDigits=r.maxDigits=6;break;
    default:
      if(e==ESP_BT_GAP_AUTH_CMPL_EVT){
        MONOPOD_LOG.printf("BT GAP authentication status=%d\n",(int)p->auth_cmpl.stat);
      }
      return false;
  }
  r.receivedMs=millis();
  portENTER_CRITICAL(&mux);request=r;portEXIT_CRITICAL(&mux);
  MONOPOD_LOG.printf("BT: authentication event %d from %s\n",(int)e,addressString(r.address).c_str());
  return true; // No unconditional acceptance by the library's default GAP handler.
}
BTPairRequest BluetoothAudioOutput::pairingRequest(){
  BTPairRequest r;portENTER_CRITICAL(&mux);r=request;portEXIT_CRITICAL(&mux);return r;
}
bool BluetoothAudioOutput::answerPairing(bool accept,const String &digits){
  BTPairRequest r=pairingRequest();
  if(r.kind==BTPairKind::None)return false;
  if(accept && r.kind!=BTPairKind::ConfirmNumber){
    if(digits.length()<r.minDigits||digits.length()>r.maxDigits)return false;
    for(size_t i=0;i<digits.length();i++)if(digits[i]<'0'||digits[i]>'9')return false;
  }
  esp_bd_addr_t a;memcpy(a,r.address,6);
  esp_err_t e=ESP_FAIL;
  if(r.kind==BTPairKind::ConfirmNumber)e=esp_bt_gap_ssp_confirm_reply(a,accept);
  else if(r.kind==BTPairKind::Passkey)e=esp_bt_gap_ssp_passkey_reply(a,accept,accept?(uint32_t)digits.toInt():0);
  else {
    esp_bt_pin_code_t pin={};
    if(accept)memcpy(pin,digits.c_str(),digits.length());
    e=esp_bt_gap_pin_reply(a,accept,accept?digits.length():0,pin);
  }
  MONOPOD_LOG.printf("BT: auth reply %d -> %d\n",(int)r.kind,(int)e);
  if(e==ESP_OK){portENTER_CRITICAL(&mux);request={};portEXIT_CRITICAL(&mux);}
  return e==ESP_OK;
}
void BluetoothAudioOutput::cancelPairing(){answerPairing(false);}
#endif // FEATURE_BLUETOOTH: keep ALL definitions behind the same guard.
