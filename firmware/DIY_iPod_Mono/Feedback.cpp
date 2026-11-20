#include "Feedback.h"
#include "Settings.h"
#include "SyncMode.h"
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
    case ClickerMode::Buzzer: return localReady;
    case ClickerMode::Headphones: return FEATURE_AUDIO;
    case ClickerMode::Both: return localReady && FEATURE_AUDIO;
    default: return false;
  }
}
const char* FeedbackController::modeName(ClickerMode mode) const {
  switch(mode){
    case ClickerMode::Off:return "Off";
    case ClickerMode::Buzzer:
#if FEATURE_HAPTIC_DRV2605
      return "Haptic";
#else
      return "Buzzer";
#endif
    case ClickerMode::Headphones:return "Headphones";
    case ClickerMode::Both:return "Both";
    default:return "Off";
  }
}
void FeedbackController::begin(){
#if FEATURE_PIEZO
  pinMode(Config::PIN_PIEZO,OUTPUT);
  digitalWrite(Config::PIN_PIEZO,LOW);
  const esp_timer_create_args_t timerArgs = {
    .callback=endPulse, .arg=this, .dispatch_method=ESP_TIMER_TASK,
    .name="monopod-click", .skip_unhandled_events=false,
  };
  localReady=esp_timer_create(&timerArgs,&pulseTimer)==ESP_OK;
  if(!localReady)MONOPOD_LOG.println("Clicker: buzzer timer unavailable");
  lastPulseEnd=millis()-Config::BUZZER_GAP_MS;
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
void FeedbackController::quiet(){
#if FEATURE_PIEZO
  pendingPulses=0;
  if(localReady)digitalWrite(Config::PIN_PIEZO,LOW);
  // Let an armed timer finish before starting another pulse. Its callback
  // only writes LOW, so muting cannot leave an old callback for a new pulse.
#endif
#if FEATURE_HAPTIC_DRV2605
  if(localReady)drv2605.stop();
#endif
#if FEATURE_AUDIO
  player.stopHeadphoneClick();
#endif
}
void FeedbackController::setRuntimeMuted(bool muted){
  suppressed=muted;
  if(muted)quiet();
}
void FeedbackController::setMode(ClickerMode mode){
  quiet();
  if(!available(mode))mode=available(ClickerMode::Buzzer)?ClickerMode::Buzzer:
                               available(ClickerMode::Headphones)?ClickerMode::Headphones:
                               ClickerMode::Off;
  selected=mode;
  if(settings.data.clicker!=selected){settings.data.clicker=selected;settings.markDirty();}
}
#if FEATURE_PIEZO
void FeedbackController::endPulse(void* arg){
  auto& self=*static_cast<FeedbackController*>(arg);
  digitalWrite(Config::PIN_PIEZO,LOW);
  self.lastPulseEnd=millis();
  self.pulseActive.store(false,std::memory_order_release);
}
#endif

void FeedbackController::loop(){
#if FEATURE_PIEZO
  if(!pendingPulses || pulseActive.load(std::memory_order_acquire))return;
  if(millis()-lastPulseEnd<Config::BUZZER_GAP_MS)return;

  // The timer returns the GPIO to LOW even if SD or TFT work stalls loop().
  pulseActive.store(true,std::memory_order_relaxed);
  digitalWrite(Config::PIN_PIEZO,HIGH);
  if(esp_timer_start_once(pulseTimer,Config::BUZZER_PULSE_MS*1000ULL)!=ESP_OK){
    endPulse(this);
    pendingPulses=0;
    MONOPOD_LOG.println("Clicker: could not start buzzer pulse");
    return;
  }
  --pendingPulses;
#endif
}

void FeedbackController::cursorMoved(uint32_t steps){
  if(!steps || syncMode.active() || suppressed || selected==ClickerMode::Off)return;
  fired+=steps;
  if(selected==ClickerMode::Buzzer || selected==ClickerMode::Both){
#if FEATURE_PIEZO
    const uint32_t room=UINT32_MAX-pendingPulses;
    pendingPulses+=steps<room?steps:room;
    loop();
#elif FEATURE_HAPTIC_DRV2605
    drv2605.stop();
    drv2605.setWaveform(0,7);
    drv2605.setWaveform(1,0);
    drv2605.go();
#endif
  }
#if FEATURE_AUDIO
  if(selected==ClickerMode::Headphones || selected==ClickerMode::Both){
    player.headphoneClick(Config::HEADPHONE_CLICK_HZ,2);
  }
#endif
}
