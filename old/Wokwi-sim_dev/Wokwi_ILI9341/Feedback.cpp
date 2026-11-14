#include "Feedback.h"
#include "Settings.h"
#include "AudioEngine.h"
#if FEATURE_HAPTIC_DRV2605
#include <Wire.h>
#include <Adafruit_DRV2605.h>
static Adafruit_DRV2605 drv2605;
#endif

FeedbackController feedback;
bool FeedbackController::available(ClickerMode mode) const {
  switch(mode){
    case ClickerMode::Off: return true;
    case ClickerMode::Speaker: return localReady;
    case ClickerMode::Headphones: return FEATURE_AUDIO;
    case ClickerMode::Both: return localReady && FEATURE_AUDIO;
    default: return false;
  }
}
const char* FeedbackController::modeName(ClickerMode mode) const {
  switch(mode){
    case ClickerMode::Off:return "Off";
    case ClickerMode::Speaker:return "Speaker";
    case ClickerMode::Headphones:return "Headphones";
    case ClickerMode::Both:return "Both";
    default:return "Off";
  }
}
ClickerMode FeedbackController::nextMode(ClickerMode current) const {
  for(int i=0;i<4;i++){
    current=(ClickerMode)(((uint8_t)current+1)%4);
    if(available(current))return current;
  }
  return ClickerMode::Off;
}
void FeedbackController::begin(){
#if FEATURE_PIEZO
  pinMode(Config::PIN_PIEZO,OUTPUT);
  digitalWrite(Config::PIN_PIEZO,LOW);
  localReady=ledcAttach(Config::PIN_PIEZO,Config::PIEZO_SCROLL_HZ,8);
  if(localReady)ledcWriteTone(Config::PIN_PIEZO,0);
#endif
#if FEATURE_HAPTIC_DRV2605
  Wire.begin(Config::PIN_HAPTIC_SDA,Config::PIN_HAPTIC_SCL);
  localReady=drv2605.begin(&Wire);
  if(localReady){
    drv2605.useLRA();
    drv2605.selectLibrary(6);
    drv2605.setMode(DRV2605_MODE_INTTRIG);
    // Calibrate the driver with the ACTUAL rated voltage of your LRA!
  }
#endif
  setMode(settings.data.clicker);
}
void FeedbackController::silencePiezo(){
#if FEATURE_PIEZO
  if(localReady)ledcWriteTone(Config::PIN_PIEZO,0);
#endif
  piezoPlaying=false;
  secondPulsePending=false;
}
void FeedbackController::setMode(ClickerMode mode){
  silencePiezo();
  if(!available(mode))mode=available(ClickerMode::Speaker)?ClickerMode::Speaker:
                               available(ClickerMode::Headphones)?ClickerMode::Headphones:
                               ClickerMode::Off;
  selected=mode;
  if(settings.data.clicker!=selected){settings.data.clicker=selected;settings.markDirty();}
}
void FeedbackController::startTone(uint16_t hz,uint8_t durationMs){
#if FEATURE_PIEZO
  if(!localReady)return;
  ledcWriteTone(Config::PIN_PIEZO,hz);
  piezoPlaying=true;piezoStopAt=millis()+durationMs;
#else
  (void)hz;(void)durationMs;
#endif
}
void FeedbackController::loop(){
  uint32_t now=millis();
  if(piezoPlaying && (int32_t)(now-piezoStopAt)>=0){
#if FEATURE_PIEZO
    ledcWriteTone(Config::PIN_PIEZO,0);
#endif
    piezoPlaying=false;
  }
  if(secondPulsePending && !piezoPlaying && (int32_t)(now-secondPulseAt)>=0){
    secondPulsePending=false;startTone(secondPulseHz,secondPulseMs);
  }
}
void FeedbackController::localEffect(FeedbackEvent event,uint32_t now){
#if FEATURE_PIEZO
  silencePiezo();
  if(event==FeedbackEvent::Scroll)startTone(Config::PIEZO_SCROLL_HZ,7);
  else if(event==FeedbackEvent::Menu)startTone(2400,12);
  else if(event==FeedbackEvent::Select)startTone(Config::PIEZO_ACTION_HZ,17);
  else {
    startTone(2900,9);
    secondPulsePending=true;
    secondPulseHz=3400;secondPulseMs=9;secondPulseAt=now+35;
  }
#elif FEATURE_HAPTIC_DRV2605
  if(localReady){
    uint8_t effect=(event==FeedbackEvent::Scroll)?7:
      (event==FeedbackEvent::Menu)?8:(event==FeedbackEvent::Select)?5:10;
    drv2605.stop();
    drv2605.setWaveform(0,effect);
    drv2605.setWaveform(1,0);
    drv2605.go();
  }
#else
  (void)event;(void)now;
#endif
}
void FeedbackController::fire(FeedbackEvent event){
  if(selected==ClickerMode::Off)return;
  uint32_t now=millis();
  if(event==FeedbackEvent::Scroll){
    if(lastScrollAt && now-lastScrollAt<Config::FEEDBACK_SCROLL_MIN_MS)return;
    lastScrollAt=now;
  }else{
    if(lastActionAt && now-lastActionAt<Config::FEEDBACK_ACTION_MIN_MS)return;
    lastActionAt=now;
  }
  if(selected==ClickerMode::Speaker || selected==ClickerMode::Both) localEffect(event,now);
  if(selected==ClickerMode::Headphones || selected==ClickerMode::Both){
#if FEATURE_AUDIO
    uint16_t hz=(event==FeedbackEvent::Scroll)?3300:
                (event==FeedbackEvent::Menu)?2500:
                (event==FeedbackEvent::Select)?2850:3150;
    uint8_t ms=(event==FeedbackEvent::Scroll)?9:
               (event==FeedbackEvent::Menu)?13:18;
    player.headphoneClick(hz,ms);
#endif
  }
}
