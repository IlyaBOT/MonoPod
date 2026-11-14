#include "Input.h"
#include "Config.h"
#include <math.h>
#include <driver/gpio.h>
InputManager input;
void InputManager::begin() {
  pinMode(Config::PIN_BUTTON_ADC, Config::BUTTON_ADC_INTERNAL_PULLUP ? INPUT_PULLUP : INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(Config::PIN_BUTTON_ADC,ADC_11db);
#if MONOPOD_BOARD == MONOPOD_BOARD_ESP32_S3
  // Reinstate pull-up after ADC config: ESP-IDF may reconfigure GPIO pads for analog input.
  if (Config::BUTTON_ADC_INTERNAL_PULLUP)
    gpio_pullup_en(static_cast<gpio_num_t>(Config::PIN_BUTTON_ADC));
#endif
  pinMode(Config::PIN_HOLD, INPUT); // external 10k pull-up is mandatory
  hold=(digitalRead(Config::PIN_HOLD)==LOW);
#if FEATURE_TOUCH_WHEEL
  // Never touch wheel while starting; average 32 baseline readings per electrode.
  uint64_t accum[6]={};
  for(int t=0;t<32;t++){
    for(int i=0;i<Config::WHEEL_COUNT;i++)accum[i]+=(uint32_t)touchRead(Config::WHEEL_PINS[i]);
    delay(2);
  }
  for(int i=0;i<Config::WHEEL_COUNT;i++)base[i]=(uint32_t)(accum[i]/32ULL);
#endif
}
void InputManager::resetState(){
  keyStable=keyLast=Key::None;
  changed=pressed=lastRepeat=millis();
  selectLongSent=false;
  wheelTouch=false;angleDelta=0;
  hold=(digitalRead(Config::PIN_HOLD)==LOW);
}
Key InputManager::readButtons() {
  // Three readings reduce noise from the weak internal pull-up / ADC acquisition.
  const int a=analogReadMilliVolts(Config::PIN_BUTTON_ADC);
  const int b=analogReadMilliVolts(Config::PIN_BUTTON_ADC);
  const int c=analogReadMilliVolts(Config::PIN_BUTTON_ADC);
  const int mV=a+b+c - min(a,min(b,c)) - max(a,max(b,c)); // median
  if (Config::BUTTON_ADC_INTERNAL_PULLUP) {
    // Nominal S3 voltages for 0/2k2/5k1/10k/22k with ~45k pull-up:
    // 0, 154, 336, 600, 1084 mV. Keep the rest of the range as no key.
    if(mV<Config::BUTTON_ADC_BOUNDS_MV[0])return Key::Menu;
    if(mV<Config::BUTTON_ADC_BOUNDS_MV[1])return Key::Previous;
    if(mV<Config::BUTTON_ADC_BOUNDS_MV[2])return Key::Next;
    if(mV<Config::BUTTON_ADC_BOUNDS_MV[3])return Key::PlayPause;
    if(mV<Config::BUTTON_ADC_BOUNDS_MV[4])return Key::Select;
    return Key::None;
  }
  // Classic ESP32: keep the original 10k external pull-up thresholds.
  if(mV<200)return Key::Menu;
  if(mV>350 && mV<820)return Key::Previous;
  if(mV>870 && mV<1290)return Key::Next;
  if(mV>1440 && mV<1840)return Key::PlayPause;
  if(mV>2010 && mV<2580)return Key::Select;
  return Key::None;
}
Key InputManager::readWheel() {
#if FEATURE_TOUCH_WHEEL
  if(millis()-lastWheel<30)return Key::None;
  lastWheel=millis();
  float x=0,y=0,total=0;
  for(int i=0;i<Config::WHEEL_COUNT;i++){
    raw[i]=touchRead(Config::WHEEL_PINS[i]);
    // Original ESP32: touched = lower count; S3: touched = higher count.
#if MONOPOD_BOARD == MONOPOD_BOARD_ESP32_S3
    float amplitude=(float)raw[i]-(float)base[i];
#else
    float amplitude=(float)base[i]-(float)raw[i];
#endif
    float threshold=max(5.0f,(float)base[i]*0.10f);
    float weight=max(0.0f,amplitude-threshold);
    const float angle=i*(6.283185307f/(float)Config::WHEEL_COUNT);
    x+=weight*cosf(angle);y+=weight*sinf(angle);total+=weight;
  }
  const bool detected=total>max(15.0f,((float)base[0])*0.10f) &&
                     hypotf(x,y)>total*0.40f;
  if(!detected){wheelTouch=false;angleDelta=0;return Key::None;}
  const float a=atan2f(y,x);
  if(!wheelTouch){wheelTouch=true;lastAngle=a;angleDelta=0;return Key::None;}
  float d=a-lastAngle;
  while(d>3.14159265f)d-=6.283185307f;
  while(d< -3.14159265f)d+=6.283185307f;
  lastAngle=a;
  // Avoid a one-sample jump when touch tracking becomes ambiguous.
  if(fabsf(d)>1.1f)return Key::None;
  angleDelta+=d;
  if(angleDelta>=0.40f){angleDelta-=0.40f;return Key::Down;}
  if(angleDelta<=-0.40f){angleDelta+=0.40f;return Key::Up;}
#endif
  return Key::None;
}
Key InputManager::poll() {
  bool nowHold=digitalRead(Config::PIN_HOLD)==LOW;
  if(hold!=nowHold){hold=nowHold;keyLast=keyStable=Key::None;selectLongSent=false;wheelTouch=false;return Key::HoldChanged;}
  if(hold)return Key::None;
  const Key b=readButtons();
  if(b!=keyLast){keyLast=b;changed=millis();}
  if(millis()-changed>=35 && keyStable!=keyLast){
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
  return readWheel();
}
