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
  MONOPOD_LOG.println("[DEV] $STATUS $BATTERY $SD $SD_RETRY $HELP");
  MONOPOD_LOG.println("[DEV] $CLICKER_MUTE=1|0 $CLICKER_STATUS");
  MONOPOD_LOG.println("[DEV] End commands with newline. No NVS persistence.");
}
void DevelopmentConsole::printStatus(){
  MONOPOD_LOG.printf("[DEV] HARDWARE_CONTROL=%s SYNC=%s QUEUED=%u\n",
    disabled?"DISABLED":"ENABLED",syncName(syncMode.transport()),(unsigned)repeats);
}
void DevelopmentConsole::handle(const char* command){
  using namespace DevConsoleProtocol;
  const Command c=parse(command);
  switch(c.type){
  case Type::Help:printHelp();return;
  case Type::Status:printStatus();return;
  case Type::Battery:
#if FEATURE_BATTERY_ADC
    MONOPOD_LOG.printf("[DEV] BAT_ADC=%lu mV RAW=%u VBAT=%.3f V PERCENT=%d GPIO=%u\n",
      (unsigned long)analogReadMilliVolts(Config::PIN_BATTERY_ADC),
      (unsigned)analogRead(Config::PIN_BATTERY_ADC),
      (double)battery.voltage(),battery.level(),(unsigned)Config::PIN_BATTERY_ADC);
#else
    MONOPOD_LOG.printf("[DEV] BATTERY_ADC=DISABLED PERCENT=%d\n",battery.level());
#endif
    return;
  case Type::Sd:
    MONOPOD_LOG.printf("[DEV] SD=%s TRACKS=%u SPI_HZ=%lu TOTAL=%llu ERROR=%s\n",
      media.sdOK()?"OK":"NOT_MOUNTED",(unsigned)media.size(),
      (unsigned long)media.mountedFrequency(),(unsigned long long)media.totalBytes(),media.lastError().c_str());
    return;
  case Type::SdRetry:
    if(syncMode.active() || player.active()){MONOPOD_LOG.println("[DEV] ERR SD_BUSY (stop playback/sync first)");return;}
    MONOPOD_LOG.println("[DEV] SD: non-destructive remount attempt...");
    if(media.begin()){MONOPOD_LOG.println("[DEV] SD MOUNT OK");ui.redraw();}
    else MONOPOD_LOG.println("[DEV] SD MOUNT FAILED: "+media.lastError());
    return;
  case Type::ClickerStatus:
    MONOPOD_LOG.printf("[DEV] CLICKER=%s ROUTE=%s EVENTS=%lu HW_CONTROL=%s\n",
      feedback.runtimeMuted()?"MUTED":"ACTIVE",feedback.modeName(settings.data.clicker),
      (unsigned long)feedback.eventCount(),disabled?"DISABLED":"ENABLED");
    return;
  case Type::ClickerMute:
    clickerMuteRequested=c.muteClicker;
    feedback.setRuntimeMuted(clickerMuteRequested || disabled);
    MONOPOD_LOG.printf("[DEV] CLICKER=%s\n",feedback.runtimeMuted()?"MUTED":"ACTIVE");
    return;
  case Type::SetHardwareDisabled:
    disabled=c.disableHardware;
    feedback.setRuntimeMuted(disabled || clickerMuteRequested);
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
