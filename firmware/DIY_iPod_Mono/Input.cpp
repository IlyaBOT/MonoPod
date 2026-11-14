#include "Input.h"
#include "Config.h"
#include <math.h>
InputManager input;
void InputManager::begin() {
#if WOKWI_SIMULATION
  for(uint8_t pin : {Config::PIN_KEY_MENU,Config::PIN_KEY_PREVIOUS,
                     Config::PIN_KEY_NEXT,Config::PIN_KEY_PLAY,Config::PIN_KEY_SELECT})
    pinMode(pin,INPUT_PULLUP);
  hold=false; // virtual schematic has no HOLD switch
#else
  pinMode(Config::PIN_BUTTON_ADC,INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(Config::PIN_BUTTON_ADC,ADC_11db);
  pinMode(Config::PIN_HOLD, INPUT); // external 10k pull-up is mandatory
  hold=(digitalRead(Config::PIN_HOLD)==LOW);
#endif
#if FEATURE_DEV_ENCODER
  pinMode(Config::ENCODER_A,INPUT);pinMode(Config::ENCODER_B,INPUT);
  encoderPrevious=(digitalRead(Config::ENCODER_A)<<1)|digitalRead(Config::ENCODER_B);
#endif
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
#if !WOKWI_SIMULATION
  hold=(digitalRead(Config::PIN_HOLD)==LOW);
#else
  hold=false;
#endif
}
Key InputManager::readButtons() {
#if WOKWI_SIMULATION
  if(!digitalRead(Config::PIN_KEY_MENU))return Key::Menu;
  if(!digitalRead(Config::PIN_KEY_PREVIOUS))return Key::Previous;
  if(!digitalRead(Config::PIN_KEY_NEXT))return Key::Next;
  if(!digitalRead(Config::PIN_KEY_PLAY))return Key::PlayPause;
  if(!digitalRead(Config::PIN_KEY_SELECT))return Key::Select;
  return Key::None;
#else
  const int mV=analogReadMilliVolts(Config::PIN_BUTTON_ADC);
  // Single key supported, measured windows for 10k pullup / 0/2k2/4k7/10k/22k resistors.
  if(mV<200)return Key::Menu;
  if(mV>350 && mV<820)return Key::Previous;
  if(mV>870 && mV<1290)return Key::Next;
  if(mV>1440 && mV<1840)return Key::PlayPause;
  if(mV>2010 && mV<2580)return Key::Select;
  return Key::None;
#endif
}
Key InputManager::readWheel() {
#if FEATURE_DEV_ENCODER
  const uint8_t current=(digitalRead(Config::ENCODER_A)<<1)|digitalRead(Config::ENCODER_B);
  if(current!=encoderPrevious){
    const uint8_t step=(encoderPrevious<<2)|current;
    if(step==0b0001||step==0b0111||step==0b1110||step==0b1000) encoderQuarter++;
    if(step==0b0010||step==0b0100||step==0b1101||step==0b1011) encoderQuarter--;
    encoderPrevious=current;
    if(encoderQuarter>=4){encoderQuarter=0;return Key::Down;}
    if(encoderQuarter<=-4){encoderQuarter=0;return Key::Up;}
  }
#endif
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
#if WOKWI_SIMULATION
  bool nowHold=false;
#else
  bool nowHold=digitalRead(Config::PIN_HOLD)==LOW;
#endif
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
