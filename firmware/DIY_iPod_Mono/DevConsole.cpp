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
  MONOPOD_LOG.println("[DEV] $STATUS $BATTERY $BUTTON_ADC $SD $HELP");
  MONOPOD_LOG.println("[DEV] $CLICKER_MUTE=1|0 $CLICKER=1|0 $CLICKER_STATUS");
  MONOPOD_LOG.println("[DEV] $WIFI_STATUS (NVS/STA/RSSI, does not show password)");
  MONOPOD_LOG.println("[DEV] End commands with newline. No NVS persistence.");
}
void DevelopmentConsole::printStatus(){
  MONOPOD_LOG.printf("[DEV] HARDWARE_CONTROL=%s SYNC=%s QUEUED=%u\n",
    disabled?"DISABLED":"ENABLED",syncName(syncMode.transport()),(unsigned)repeats);
}
void DevelopmentConsole::handle(const char* command){
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
    MONOPOD_LOG.printf("[DEV] BATTERY=%.3f V PERCENT=%d\n",
      (double)battery.voltage(),battery.level());
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
