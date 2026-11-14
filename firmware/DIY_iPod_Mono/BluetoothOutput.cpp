#include "BluetoothOutput.h"
#if FEATURE_BLUETOOTH
#include <cstring>
int16_t BluetoothAudioOutput::pcm[CAPACITY*2]={};
volatile uint16_t BluetoothAudioOutput::rd=0, BluetoothAudioOutput::wr=0;
portMUX_TYPE BluetoothAudioOutput::mux=portMUX_INITIALIZER_UNLOCKED;
BluetoothA2DPSource BluetoothAudioOutput::device;
bool BluetoothAudioOutput::initialized=false;
uint8_t BluetoothAudioOutput::foundCount=0;
BTHeadset BluetoothAudioOutput::found[16]={};
BluetoothAudioOutput::BluetoothAudioOutput(){hertz=44100;channels=2;SetGain(.1f);}
bool BluetoothAudioOutput::begin(){
  portENTER_CRITICAL(&mux);rd=wr=0;portEXIT_CRITICAL(&mux);
  if(!initialized){
    device.set_data_callback(callback);
    device.set_ssid_callback(discoverCallback);
    device.set_ssp_enabled(true);
    // Start scanning, but do not auto-connect to an arbitrary audio device.
    initialized=true;
    device.start();
  }
  return true;
}
bool BluetoothAudioOutput::SetRate(int hz){hertz=hz;return hz==44100;}
bool BluetoothAudioOutput::SetChannels(int channels){this->channels=channels;return channels==1||channels==2;}
bool BluetoothAudioOutput::ConsumeSample(int16_t sample[2]){
  portENTER_CRITICAL(&mux);
  uint16_t next=(wr+1)%CAPACITY;
  if(next==rd){portEXIT_CRITICAL(&mux);return false;}
  pcm[wr*2]=Amplify(sample[0]);pcm[wr*2+1]=Amplify(channels==1?sample[0]:sample[1]);
  wr=next;portEXIT_CRITICAL(&mux);return true;
}
int32_t BluetoothAudioOutput::callback(uint8_t *out,int32_t bytes){
  if(!out||bytes<0)return 0;
  const int32_t frames=bytes/4;
  int16_t* target=(int16_t*)out;
  for(int32_t i=0;i<frames;i++){
    portENTER_CRITICAL(&mux);
    if(rd!=wr){target[2*i]=pcm[rd*2];target[2*i+1]=pcm[rd*2+1];rd=(rd+1)%CAPACITY;}
    else {target[2*i]=0;target[2*i+1]=0;}
    portEXIT_CRITICAL(&mux);
  }
  return frames*4;
}
bool BluetoothAudioOutput::stop(){
  // Do not destroy Classic BT stack when changing tracks; it is reused.
  portENTER_CRITICAL(&mux);rd=wr=0;portEXIT_CRITICAL(&mux);
  return true;
}
#endif

bool BluetoothAudioOutput::discoverCallback(const char* name,esp_bd_addr_t address,int rssi){
  if(!name||!name[0])return false;
  portENTER_CRITICAL(&mux);
  int foundAt=-1;
  for(int i=0;i<foundCount;i++)
    if(memcmp(found[i].address,address,6)==0){foundAt=i;break;}
  if(foundAt<0 && foundCount<16)foundAt=foundCount++;
  if(foundAt>=0){
    strncpy(found[foundAt].name,name,sizeof(found[foundAt].name)-1);
    found[foundAt].name[sizeof(found[foundAt].name)-1]=0;
    memcpy(found[foundAt].address,address,6);found[foundAt].rssi=rssi;
  }
  portEXIT_CRITICAL(&mux);
  return false; // Only connect after explicit Select in the player menu.
}
uint8_t BluetoothAudioOutput::deviceCount(){
  portENTER_CRITICAL(&mux);uint8_t n=foundCount;portEXIT_CRITICAL(&mux);return n;
}
BTHeadset BluetoothAudioOutput::discovered(uint8_t i){
  BTHeadset x={};portENTER_CRITICAL(&mux);
  if(i<foundCount)x=found[i];portEXIT_CRITICAL(&mux);return x;
}
bool BluetoothAudioOutput::connectDevice(uint8_t i){
  BTHeadset h=discovered(i);
  if(!h.name[0])return false;
  esp_bd_addr_t addr;memcpy(addr,h.address,6);
  return device.connect_to(addr);
}
bool BluetoothAudioOutput::connected(){return initialized && device.is_connected();}
bool BluetoothAudioOutput::scanning(){return initialized && device.is_discovery_active();}
