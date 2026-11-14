#include "Input.h"
#include "Config.h"
#include <math.h>
#include <driver/gpio.h>
InputManager input;
void InputManager::begin() {
  pinMode(Config::PIN_BUTTON_ADC, INPUT);
#if MONOPOD_BOARD == MONOPOD_BOARD_ESP32_S3
  buttonSupplyCompensation.reset();
#endif
  analogReadResolution(12);
  analogSetPinAttenuation(Config::PIN_BUTTON_ADC,ADC_11db);

  pinMode(Config::PIN_HOLD, INPUT); // external 10k pull-up is mandatory
  hold=(digitalRead(Config::PIN_HOLD)==LOW);
  awaitingIdle=true;idleSince=0;
#if FEATURE_TOUCH_WHEEL
  // Never touch the wheel during boot calibration.
  touchCalibrate();
#endif
}
void InputManager::resetState(){
  keyStable=keyLast=Key::None;
  changed=pressed=lastRepeat=millis();
  selectLongSent=false;
  touchResetTracking();
  hold=(digitalRead(Config::PIN_HOLD)==LOW);
  awaitingIdle=true;idleSince=0;
}
uint16_t InputManager::buttonAdcMillivolts() {
  const int a=analogReadMilliVolts(Config::PIN_BUTTON_ADC);
  const int b=analogReadMilliVolts(Config::PIN_BUTTON_ADC);
  const int c=analogReadMilliVolts(Config::PIN_BUTTON_ADC);
  return (uint16_t)(a+b+c-min(a,min(b,c))-max(a,max(b,c)));
}
Key InputManager::decodeButtonMillivolts(uint16_t mV) const {
#if MONOPOD_BOARD == MONOPOD_BOARD_ESP32_S3
  // Compare measured fraction of GPIO9 idle voltage (3V3 rail), rather than
  // fixed mV thresholds. ADC's own internal reference is not rail-ratiometric.
  switch (buttonSupplyCompensation.decode(mV)) {
    case ButtonAdcCompensation::Button::Menu: return Key::Menu;
    case ButtonAdcCompensation::Button::Previous: return Key::Previous;
    case ButtonAdcCompensation::Button::Next: return Key::Next;
    case ButtonAdcCompensation::Button::PlayPause: return Key::PlayPause;
    case ButtonAdcCompensation::Button::Select: return Key::Select;
    default: return Key::None;
  }
#endif
  if(mV<200)return Key::Menu;
  if(mV>350 && mV<820)return Key::Previous;
  if(mV>870 && mV<1290)return Key::Next;
  if(mV>1440 && mV<1840)return Key::PlayPause;
  if(mV>2010 && mV<2580)return Key::Select;
  return Key::None;
}
Key InputManager::readButtons() {
  const uint16_t readingMv = buttonAdcMillivolts();
#if MONOPOD_BOARD == MONOPOD_BOARD_ESP32_S3
  // Update only from released GPIO9. While held, reuse the latest estimate.
  buttonSupplyCompensation.observe(readingMv);
#endif
  return decodeButtonMillivolts(readingMv);
}
void InputManager::touchResetTracking() {
  wheelTouch=false;
  touchCandidateSince=0;
  touchMissingSince=0;
  wheelIntegrator.reset();
  touchPresent=false;
  eventFromWheel=false;
}
int32_t InputManager::wheelDelta(uint8_t i) const {
  if(i>=Config::WHEEL_COUNT)return 0;
  const int64_t difference=(int64_t)raw[i]-(int64_t)base[i];
#if MONOPOD_BOARD == MONOPOD_BOARD_ESP32_S3
  return (int32_t)constrain(difference, (int64_t)INT32_MIN, (int64_t)INT32_MAX);
#else
  return (int32_t)constrain(-difference, (int64_t)INT32_MIN, (int64_t)INT32_MAX);
#endif
}
void InputManager::touchCalibrate() {
#if FEATURE_TOUCH_WHEEL
  // ESP32-S3 touchRead can yield startup transients much higher than stable values.
  // Warm up the entire sensor bank before using ANY samples as a baseline.
  for(uint8_t n=0;n<48;n++){
    for(uint8_t i=0;i<Config::WHEEL_COUNT;i++)
      (void)touchRead(Config::WHEEL_PINS[i]);
    delay(5);
  }
  // 32 stable samples per electrode, accumulated in 64-bit integers.
  uint64_t accum[6]={};
  for(uint8_t n=0;n<32;n++){
    for(uint8_t i=0;i<Config::WHEEL_COUNT;i++){
      accum[i]+=(uint32_t)touchRead(Config::WHEEL_PINS[i]);
    }
    delay(2);
  }
  for(uint8_t i=0;i<Config::WHEEL_COUNT;i++){
    base[i]=(uint32_t)(accum[i]/32ULL);
    raw[i]=base[i];
  }
  touchValid=false;
  touchSum=touchVector=touchX=touchY=0;
  touchResetTracking();
#endif
}
void InputManager::touchSample() {
#if FEATURE_TOUCH_WHEEL
  touchValid=true;
  touchX=touchY=touchSum=touchVector=0;
  // Recover from a bad startup calibration (e.g. baseline 160k, raw 31k).
  // Do NOT attempt this for ordinary finger touches (they increase S3 readings).
  bool badBaseline=false;
  for(uint8_t i=0;i<Config::WHEEL_COUNT;i++){
    const uint32_t reading=(uint32_t)touchRead(Config::WHEEL_PINS[i]);
    if(reading>0 && base[i]>0 &&
       ((uint64_t)reading*2u<(uint64_t)base[i] ||
        (uint64_t)reading>(uint64_t)base[i]*4u))badBaseline=true;
    raw[i]=reading;
  }
  if(badBaseline){
    if(badBaselineSamples<4)++badBaselineSamples;
    if(badBaselineSamples>=4){
      touchCalibrate();
      badBaselineSamples=0;
    }
    touchValid=false;
    touchPresent=false;
    touchResetTracking();
    return;
  }
  badBaselineSamples=0;
  float baselineSum=0,peakWeight=0;
  for(uint8_t i=0;i<Config::WHEEL_COUNT;i++){
    // Invalid readings must never generate a UI event.
    if(raw[i]==0 || raw[i]==UINT32_MAX || base[i]==0)touchValid=false;
    baselineSum+=(float)base[i];
    const float delta=(float)wheelDelta(i);
    const uint8_t pct=TOUCH_SENS_PCT;
    const float threshold=max((float)TOUCH_MIN_DELTA,(float)base[i]*(float)pct/100.0f);
    const float weight=max(0.0f,delta-threshold);
    if(weight>peakWeight)peakWeight=weight;
    const float angle=i*(6.283185307f/(float)Config::WHEEL_COUNT);
    touchX+=weight*cosf(angle);touchY+=weight*sinf(angle);touchSum+=weight;
  }
  touchVector=hypotf(touchX,touchY);
  const float meanBase=baselineSum/(float)Config::WHEEL_COUNT;
  // Reject low-level and spatially ambiguous noise with proven fixed thresholds.
  touchPresent=touchValid && peakWeight>0 &&
    touchSum>max(15.0f,meanBase*(float)TOUCH_TOTAL_PCT/100.0f) &&
    touchVector>touchSum*(float)TOUCH_DOM_PCT/100.0f;
#endif
}
Key InputManager::readWheel() {
#if FEATURE_TOUCH_WHEEL
  const uint32_t now=millis();
  if((uint32_t)(now-lastWheel)<30)return Key::None;
  lastWheel=now;
  touchSample();
  // Preserve the touch track through brief dead zones between electrodes.
  if(!touchPresent){
    if(touchMissingSince==0)touchMissingSince=now;
    if((uint32_t)(now-touchMissingSince)>130)touchResetTracking();
    return Key::None;
  }
  const bool wasMissing=touchMissingSince!=0;
  const uint32_t missingMs=wasMissing?(uint32_t)(now-touchMissingSince):0;
  touchMissingSince=0;
  const float measured=atan2f(touchY,touchX); // interpolated six-pad angle
  if(!wheelTouch){
    if(touchCandidateSince==0)touchCandidateSince=now;
    if((uint32_t)(now-touchCandidateSince)<35)return Key::None;
    wheelTouch=true;wheelIntegrator.reset();wheelIntegrator.anchor(measured);
    return Key::None;
  }
  if(wasMissing && missingMs>75){
    wheelIntegrator.anchor(measured);return Key::None;
  }
  const int direction=wheelIntegrator.sample(measured);
  if(direction>0){eventFromWheel=true;return Key::Down;}
  if(direction<0){eventFromWheel=true;return Key::Up;}

#endif
  return Key::None;
}
Key InputManager::poll() {
  eventFromWheel=false;
  bool nowHold=digitalRead(Config::PIN_HOLD)==LOW;
  if(hold!=nowHold){
    hold=nowHold;keyLast=keyStable=Key::None;
    selectLongSent=false;touchResetTracking();
    awaitingIdle=true;idleSince=0;
    return Key::HoldChanged;
  }
  if(hold)return Key::None;
  const Key b=readButtons();
  // After boot / HOLD / developer-console reset, demand a stable released state.
  // Otherwise a key pressed during reset can spontaneously trigger a menu action.
  if(awaitingIdle){
    if(b!=Key::None)idleSince=0;
    else if(idleSince==0)idleSince=millis();
    else if((uint32_t)(millis()-idleSince)>=Config::BUTTON_ADC_RELEASE_GUARD_MS){
      awaitingIdle=false;
      keyLast=keyStable=Key::None;
      changed=millis();
    }
    return Key::None;
  }
  if(b!=keyLast){keyLast=b;changed=millis();}
  if(millis()-changed>=Config::BUTTON_ADC_DEBOUNCE_MS && keyStable!=keyLast){
    const Key old=keyStable;
    keyStable=keyLast;pressed=lastRepeat=millis();
    if(old==Key::Select && keyStable!=Key::Select && !selectLongSent){
      selectLongSent=false;
      return Key::Select; // Short select fires on release, never after a long press.
    }
    if(keyStable==Key::Select){selectLongSent=false;}
    else if(keyStable!=Key::None)return keyStable;
  }
  if(keyStable==Key::Select && !selectLongSent && millis()-pressed>=1500){
    selectLongSent=true;
    return Key::SelectLong;
  }
  if(keyStable==Key::Previous || keyStable==Key::Next){
    if(millis()-pressed>600 && millis()-lastRepeat>160){lastRepeat=millis();return keyStable;}
  }
  // A mechanical button must never trigger an additional capacitive wheel event.
  if(b!=Key::None || keyStable!=Key::None){touchResetTracking();return Key::None;}
  return readWheel();
}
