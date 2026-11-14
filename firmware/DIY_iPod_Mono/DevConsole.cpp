#include "DevConsole.h"
#include "DevConsoleProtocol.h"
#include "Config.h"
#include "Input.h"
#include "Battery.h"
#include "Library.h"
#include "SyncMode.h"
#include "UI.h"
#include "Feedback.h"
#include "AudioEngine.h"
#include "Settings.h"
#include "WiFiNetworks.h"
#include <WiFi.h>
#include <stdlib.h>
#include <limits.h>

DevelopmentConsole devConsole;
namespace {
Key toKey(DevConsoleProtocol::Button b) {
  using B=DevConsoleProtocol::Button;
  switch(b) {
  case B::Up:return Key::Up;
  case B::Down:return Key::Down;
  case B::Menu:return Key::Menu;
  case B::Select:return Key::Select;
  case B::SelectLong:return Key::SelectLong;
  case B::Previous:return Key::Previous;
  case B::Next:return Key::Next;
  case B::PlayPause:return Key::PlayPause;
  default:return Key::None;
  }
}
const char* syncName(SyncTransport s) {
  switch(s) {
  case SyncTransport::UsbCdc:return "USB_CDC";
  case SyncTransport::UsbMsc:return "USB_MSC";
  case SyncTransport::WiFi:return "WIFI";
  default:return "NONE";
  }
}
}
void DevelopmentConsole::begin(){
  MONOPOD_LOG.println("[DEV] UART console ready at 115200 baud. Type $HELP");
}
void DevelopmentConsole::printHelp(){
  MONOPOD_LOG.println("[DEV] $DISABLE_HARDWARE_CONTROL=1|0");
  MONOPOD_LOG.println("[DEV] $MENU $SELECT $SELECT_LONG $UP $DOWN $PREV $NEXT $PLAY");
  MONOPOD_LOG.println("[DEV] $KEY=MENU|SELECT|SELECT_LONG|UP|DOWN|PREV|NEXT|PLAY_PAUSE");
  MONOPOD_LOG.println("[DEV] $SCROLL=UP,5 | $SCROLL=DOWN,5 (max 32)");
  MONOPOD_LOG.println("[DEV] $STATUS $BATTERY $BUTTON_ADC $BTNS_ADC[=1|0] $SD $HELP");
  MONOPOD_LOG.println("[DEV] BTN S3: MENU=0 PREV=5k1 NEXT=10k PLAY=20k SELECT=39k9 (GND)");
  MONOPOD_LOG.println("[DEV] $TOUCH | $TOUCH_DEBUG=1|0 | $TOUCH_CAL (release wheel first)");
  MONOPOD_LOG.println("[DEV] $TOUCH_ENABLE=1|0 (disable only capacitive wheel)");
  MONOPOD_LOG.println("[DEV] $TOUCH_SENS=10 (relative % of each baseline, 1..80)");
  MONOPOD_LOG.println("[DEV] $TOUCH_TOTAL=10 (sum threshold, % of mean baseline, 1..80)");
  MONOPOD_LOG.println("[DEV] $TOUCH_MIN=5 (minimum raw delta, 1..100000)");
  MONOPOD_LOG.println("[DEV] $TOUCH_PAD=1,15 (pad 1..6, 0 resets pad override)");
  MONOPOD_LOG.println("[DEV] $TOUCH_DOM=50 (directional dominance %, 20..95)");
  MONOPOD_LOG.println("[DEV] Lower SENS/TOTAL/MIN = more sensitive; increase to reject noise.");
  MONOPOD_LOG.println("[DEV] Touch tuning is runtime-only: reboot restores defaults.");
  MONOPOD_LOG.println("[DEV] $CLICKER_MUTE=1|0 $CLICKER=1|0 $CLICKER_STATUS");
  MONOPOD_LOG.println("[DEV] $WIFI_STATUS (NVS/STA/RSSI, does not show password)");
  MONOPOD_LOG.println("[DEV] End commands with newline. No NVS persistence.");
}
void DevelopmentConsole::printStatus(){
  MONOPOD_LOG.printf("[DEV] HARDWARE_CONTROL=%s SYNC=%s QUEUED=%u\n",
    disabled?"DISABLED":"ENABLED",syncName(syncMode.transport()),(unsigned)repeats);
}
namespace {
const char* physicalKeyName(Key key){
  switch(key){
  case Key::Menu:return "MENU";case Key::Previous:return "PREV";
  case Key::Next:return "NEXT";case Key::PlayPause:return "PLAY";
  case Key::Select:return "SELECT";case Key::SelectLong:return "SELECT_LONG";
  case Key::HoldChanged:return "HOLD_CHANGED";
  default:return "OTHER";
  }
}
}
void DevelopmentConsole::printButtonsSample(bool force){
  if(!force && !buttonsDebug)return;
  const uint16_t mv=input.buttonAdcMillivolts();
  const Key key=input.decodeButtonMillivolts(mv);
  const bool hold=(digitalRead(Config::PIN_HOLD)==LOW);
  const int code=(int)key+(hold?100:0);
  const uint32_t now=millis();
  // 10Hz maximum; print on button transition or a 1s heartbeat.
  if(!force && ((uint32_t)(now-lastButtonsLog)<100 ||
     (code==lastButtonsCode && (uint32_t)(now-lastButtonsLog)<1000)))return;
  lastButtonsLog=now;lastButtonsCode=code;
  const int raw=analogRead(Config::PIN_BUTTON_ADC);
  MONOPOD_LOG.printf("[DEV] BTNS_ADC=%u mV RAW=%d KEY=%s HOLD=%s PULLUP=%s ARMED=%s\n",
    (unsigned)mv,raw,key==Key::None?"NONE":physicalKeyName(key),
    hold?"ON":"OFF",Config::BUTTON_ADC_INTERNAL_PULLUP?"INTERNAL":"EXTERNAL",
    input.buttonsReady()?"YES":"NO");
}
void DevelopmentConsole::printTouchSettings() {
  MONOPOD_LOG.printf("[DEV] TOUCH ENABLE=%s DEBUG=%s SENS=%u%% TOTAL=%u%% MIN=%lu DOM=%u%% TRACK=%s ACTIVE=%s\n",
    input.touchEnabled()?"YES":"NO",touchDebug?"ON":"OFF",
    (unsigned)input.touchSensitivity(),(unsigned)input.touchTotalThreshold(),
    (unsigned long)input.touchMinimumDelta(),(unsigned)input.touchDominance(),
    input.touchTracking()?"YES":"NO",input.touchDetected()?"YES":"NO");
  MONOPOD_LOG.print("[DEV] TOUCH_PAD_SENS (0=global)");
  for(uint8_t i=0;i<Config::WHEEL_COUNT;i++)
    MONOPOD_LOG.printf(" GPIO%u=%u",(unsigned)Config::WHEEL_PINS[i],(unsigned)input.touchPadSensitivity(i));
  MONOPOD_LOG.println();
}
void DevelopmentConsole::printTouchSample(bool force) {
#if FEATURE_TOUCH_WHEEL
  if(!force && !touchDebug)return;
  const uint32_t now=millis();
  if(!force && (uint32_t)(now-lastTouchLog)<250u)return;
  lastTouchLog=now;
  // A diagnostic read ONLY samples the electrodes. It cannot inject UP/DOWN.
  input.touchSample();
  MONOPOD_LOG.printf("[DEV] TOUCH VALID=%s DETECT=%s TRACK=%s PEAK=%u WEIGHT=%.1f VECTOR=%.1f\n",
    input.touchSampleValid()?"YES":"NO",input.touchDetected()?"YES":"NO",
    input.touchTracking()?"YES":"NO",(unsigned)(input.touchPeakChannel()+1),
    (double)input.touchWeightSum(),(double)input.touchVectorLength());
  MONOPOD_LOG.print("[DEV] TOUCH_RAW");
  for(uint8_t i=0;i<Config::WHEEL_COUNT;i++)
    MONOPOD_LOG.printf(" GPIO%u=%lu",(unsigned)Config::WHEEL_PINS[i],(unsigned long)input.wheelRaw(i));
  MONOPOD_LOG.println();
  MONOPOD_LOG.print("[DEV] TOUCH_BASE");
  for(uint8_t i=0;i<Config::WHEEL_COUNT;i++)
    MONOPOD_LOG.printf(" GPIO%u=%lu",(unsigned)Config::WHEEL_PINS[i],(unsigned long)input.wheelBase(i));
  MONOPOD_LOG.println();
  MONOPOD_LOG.print("[DEV] TOUCH_DELTA");
  for(uint8_t i=0;i<Config::WHEEL_COUNT;i++)
    MONOPOD_LOG.printf(" GPIO%u=%ld",(unsigned)Config::WHEEL_PINS[i],(long)input.wheelDelta(i));
  MONOPOD_LOG.println();
#else
  (void)force;
  MONOPOD_LOG.println("[DEV] TOUCH=COMPILED_OUT");
#endif
}
namespace {
// Recognize numeric tuning commands only when the complete parameter is valid.
bool touchOption(const char* command,const char* prefix,uint32_t& value) {
  const size_t length=strlen(prefix);
  if(strncmp(command,prefix,length)!=0)return false;
  const char* digits=command+length;
  if(!*digits)return false;
  for(const char* c=digits;*c;c++)if(*c<'0'||*c>'9')return false;
  const unsigned long parsed=strtoul(digits,nullptr,10);
  if(parsed>100000UL)return false;
  value=(uint32_t)parsed;
  return true;
}
}
void DevelopmentConsole::physicalKey(Key key){
  if(touchDebug && (key==Key::Up || key==Key::Down)) {
    MONOPOD_LOG.printf("[DEV] TOUCH_EVENT=%s\n",key==Key::Up?"UP":"DOWN");
  }
  if(!buttonsDebug)return;
  if(key==Key::Menu || key==Key::Previous || key==Key::Next ||
     key==Key::PlayPause || key==Key::Select || key==Key::SelectLong ||
     key==Key::HoldChanged){
    MONOPOD_LOG.printf("[DEV] PHYSICAL_KEY=%s\n",physicalKeyName(key));
    printButtonsSample(true);
  }
}
void DevelopmentConsole::handle(const char* command){
  if(strcmp(command,"$TOUCH")==0 || strcmp(command,"$TOUCH_STATUS")==0){
    printTouchSettings();printTouchSample(true);return;
  }
  if(strcmp(command,"$TOUCH_DEBUG=1")==0 || strcmp(command,"$TOUCH_DEBUG=0")==0){
    touchDebug=command[strlen(command)-1]=='1';
    lastTouchLog=0;
    MONOPOD_LOG.printf("[DEV] TOUCH_DEBUG=%s; raw/base/delta every 250ms\n",touchDebug?"ON":"OFF");
    if(touchDebug){printTouchSettings();printTouchSample(true);}
    return;
  }
  if(strcmp(command,"$TOUCH_CAL")==0){
    MONOPOD_LOG.println("[DEV] TOUCH_CAL: measuring released wheel (32 samples x 6)...");
    input.touchCalibrate();printTouchSettings();printTouchSample(true);return;
  }
  if(strncmp(command,"$TOUCH_PAD=",11)==0){
    const char* value=command+11;
    const char* comma=strchr(value,',');
    if(comma && comma>value && comma[1]){
      bool valid=true;
      for(const char* c=value;c<comma;c++)if(*c<'0'||*c>'9')valid=false;
      for(const char* c=comma+1;*c;c++)if(*c<'0'||*c>'9')valid=false;
      if(valid){
        const unsigned long channel=strtoul(value,nullptr,10);
        const unsigned long pct=strtoul(comma+1,nullptr,10);
        if(channel>=1 && channel<=Config::WHEEL_COUNT && pct<=80 &&
           (pct==0 || pct>=1)){
          input.touchSetPadSensitivity((uint8_t)(channel-1),(uint8_t)pct);
          printTouchSettings();return;
        }
      }
    }
    MONOPOD_LOG.println("[DEV] ERR TOUCH_PAD: expected $TOUCH_PAD=1..6,0..80");return;
  }
  uint32_t touchValue=0;
  if(touchOption(command,"$TOUCH_ENABLE=",touchValue) && touchValue<=1){
    input.touchEnable(touchValue!=0);printTouchSettings();return;
  }
  if(touchOption(command,"$TOUCH_SENS=",touchValue) && touchValue>=1 && touchValue<=80){
    input.touchSetSensitivity((uint8_t)touchValue);printTouchSettings();return;
  }
  if(touchOption(command,"$TOUCH_TOTAL=",touchValue) && touchValue>=1 && touchValue<=80){
    input.touchSetTotalThreshold((uint8_t)touchValue);printTouchSettings();return;
  }
  if(touchOption(command,"$TOUCH_MIN=",touchValue) && touchValue>=1){
    input.touchSetMinimumDelta(touchValue);printTouchSettings();return;
  }
  if(touchOption(command,"$TOUCH_DOM=",touchValue) && touchValue>=20 && touchValue<=95){
    input.touchSetDominance((uint8_t)touchValue);printTouchSettings();return;
  }
  if(strncmp(command,"$TOUCH_",7)==0){
    MONOPOD_LOG.println("[DEV] ERR TOUCH_PARAMETER (try $HELP)");return;
  }
  if(strcmp(command,"$BTNS_ADC")==0 || strcmp(command,"$BTNS_ADC=1")==0){
    buttonsDebug=true;
    MONOPOD_LOG.println("[DEV] BTNS_ADC streaming ON (transitions + 1s heartbeat). Press each key separately.");
    printButtonsSample(true);return;
  }
  if(strcmp(command,"$BTNS_ADC=0")==0){
    buttonsDebug=false;MONOPOD_LOG.println("[DEV] BTNS_ADC streaming OFF");return;
  }
  // The S3 internal ADC pull-up is weak and varies across chips. Capture the
  // actual voltage for each button rather than assuming nominal 45k resistance.
  if(strcmp(command,"$BUTTON_ADC")==0){
    const uint32_t mv=analogReadMilliVolts(Config::PIN_BUTTON_ADC);
    MONOPOD_LOG.printf("[DEV] BUTTON_ADC=%lu mV PIN=%u INTERNAL_PULLUP=%s\n",
      (unsigned long)mv,(unsigned)Config::PIN_BUTTON_ADC,
      Config::BUTTON_ADC_INTERNAL_PULLUP?"YES":"NO");
    return;
  }
  using namespace DevConsoleProtocol;
  const Command c=parse(command);
  switch(c.type){
  case Type::Help:printHelp();return;
  case Type::Status:printStatus();return;
  case Type::Battery:
#if FEATURE_BATTERY_ADC
    MONOPOD_LOG.printf("[DEV] BATTERY_EST=%.3f V PERCENT=%d ADC=%u mV CHARGING=%s ADC_STATUS=%s\n",
      (double)battery.voltage(),battery.level(),
      (unsigned)battery.adcMillivolts(),battery.charging()?"YES":"NO",battery.adcStatus());
#else
    MONOPOD_LOG.printf("[DEV] BATTERY_ADC=DISABLED PERCENT=%d\n",battery.level());
#endif
    return;
  case Type::WifiStatus:
    MONOPOD_LOG.printf("[DEV] WIFI=%s SAVED=%s CONNECTING=%s BARS=%u RSSI=%ld dBm SSID=%s\n",
      wifiNetworks.connected()?"CONNECTED":(wifiNetworks.connecting()?"CONNECTING":"OFFLINE"),
      wifiNetworks.hasSavedNetwork()?"YES":"NO",wifiNetworks.connecting()?"YES":"NO",
      (unsigned)wifiNetworks.signalBars(),
      wifiNetworks.connected()?(long)WiFi.RSSI():0L,
      wifiNetworks.connectedSSID().c_str());
    return;
  case Type::Sd:
    MONOPOD_LOG.printf("[DEV] SD=%s TRACKS=%u SPI_HZ=%lu TOTAL=%llu ERROR=%s\n",
      media.sdOK()?"OK":"NOT_MOUNTED",(unsigned)media.size(),
      (unsigned long)media.mountedFrequency(),(unsigned long long)media.totalBytes(),media.lastError().c_str());
    return;
  case Type::ClickerStatus:
    MONOPOD_LOG.printf("[DEV] CLICKER=%s ROUTE=%s EVENTS=%lu HW_CONTROL=%s\n",
      feedback.runtimeMuted()?"MUTED":"ACTIVE",feedback.modeName(settings.data.clicker),
      (unsigned long)feedback.eventCount(),disabled?"DISABLED":"ENABLED");
    return;
  case Type::ClickerMute:
    clickerMuteRequested=c.muteClicker;
    feedback.setRuntimeMuted(clickerMuteRequested);
    MONOPOD_LOG.printf("[DEV] CLICKER=%s\n",feedback.runtimeMuted()?"MUTED":"ACTIVE");
    return;
  case Type::SetHardwareDisabled:
    disabled=c.disableHardware;
    // Disable PHYSICAL input only. The developer needs audible feedback for
    // virtual $SELECT/$UP/etc. independent of floating/unassembled GPIOs.
    // Silence any click already queued by the hardware before switching.
    feedback.quiet();
    feedback.setRuntimeMuted(clickerMuteRequested);
    repeats=0;queued=Key::None;keySequenceStarted=false;
    input.resetState();
    ui.redraw();
    MONOPOD_LOG.printf("[DEV] OK HARDWARE_CONTROL=%s (runtime only)\n",disabled?"DISABLED":"ENABLED");
    return;
  case Type::Key:case Type::Scroll:
    if(syncMode.active() && !(syncMode.is(SyncTransport::WiFi) && c.button==Button::SelectLong)){
      MONOPOD_LOG.println("[DEV] ERR SYNC_LOCKED");return;
    }
    if(repeats){MONOPOD_LOG.println("[DEV] ERR KEY_QUEUE_BUSY");return;}
    queued=toKey(c.button);repeats=c.count;keySequenceStarted=false;
    MONOPOD_LOG.printf("[DEV] OK KEY_QUEUED=%u\n",(unsigned)repeats);
    return;
  case Type::Invalid:default:
    MONOPOD_LOG.println("[DEV] ERR UNKNOWN_COMMAND (type $HELP)");return;
  }
}
void DevelopmentConsole::loop(){
  printButtonsSample(false);
  printTouchSample(false);
  // Never read native USB CDC Serial here: USB Sync v1 uses binary MPOD1 frames.
  // When FEATURE_USB_SYNC=1, MONOPOD_LOG is Serial0 (the UART0 bridge).
  uint8_t budget=64;
  while(budget-- && MONOPOD_LOG.available()>0){
    const int byte=MONOPOD_LOG.read();
    if(byte<0)break;
    if(byte=='\r')continue;
    if(byte=='\n'){
      if(overflow)MONOPOD_LOG.println("[DEV] ERR LINE_TOO_LONG");
      else if(length){line[length]='\0';handle(line);}
      length=0;overflow=false;continue;
    }
    if(byte<32||byte>126)continue; // ASCII command vocabulary only
    if(overflow)continue;
    if(length>=CONSOLE_LINE_CAPACITY){length=0;overflow=true;continue;}
    line[length++]=(char)byte;
  }
}
Key DevelopmentConsole::pollKey(){
  if(!repeats)return Key::None;
  // Scroll at most one step per 45 ms: prevents redrawing 32 times in a burst.
  if(keySequenceStarted && millis()-lastKeyAt<45)return Key::None;
  const Key key=queued;
  lastKeyAt=millis();keySequenceStarted=true;
  --repeats;
  if(!repeats){queued=Key::None;keySequenceStarted=false;}
  return key;
}
