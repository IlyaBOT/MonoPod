#include "Input.h"
#include "Config.h"
#include <math.h>
InputManager input;
void InputManager::begin() {
#if WOKWI_SIMULATION
  for(uint8_t pin : {Config::PIN_KEY_MENU,Config::PIN_KEY_PREVIOUS,
                     Config::PIN_KEY_NEXT,Config::PIN_KEY_PLAY,Config::PIN_KEY_SELECT})
    pinMode(pin,pin >= 34 ? INPUT : INPUT_PULLUP);
#if FEATURE_SIM_WHEEL
  for (uint8_t i=0;i<6;i++) pinMode(Config::WHEEL_PINS[i], INPUT);
  simWheelPosition=-1;
#endif
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
  uint32_t accum[6]={};
  for(int t=0;t<32;t++){
    for(int i=0;i<Config::WHEEL_COUNT;i++)accum[i]+=(uint32_t)touchRead(Config::WHEEL_PINS[i]);
    delay(2);
  }
  for(int i=0;i<Config::WHEEL_COUNT;i++)base[i]=min(65535UL,accum[i]/32UL);
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
#if WOKWI_SIMULATION && FEATURE_SIM_WHEEL
  // Wokwi custom chip E0..E5: DIGITAL proxy, not the ESP32 capacitance unit.
  // One active pad => even step (0,2,4,...); adjacent pair => odd midpoint.
  if (millis() - lastWheel < 12) return Key::None;
  lastWheel=millis();
  bool active[6]={};
  int count=0, first=-1, second=-1;
  for(int i=0;i<6;i++){
    active[i] = digitalRead(Config::WHEEL_PINS[i]) == HIGH;
    if(active[i]){if(first<0)first=i;else second=i;count++;}
  }
  if(count<1 || count>2){simWheelPosition=-1;return Key::None;}
  int pos=-1;
  if(count==1){pos=first*2;}
  else if(((first+1)%6)==second){pos=first*2+1;}
  else if(first==0 && second==5){pos=11;}
  if(pos<0){simWheelPosition=-1;return Key::None;}
  if(simWheelPosition<0){simWheelPosition=pos;return Key::None;}
  int d=(pos-simWheelPosition+12)%12;
  if(d>6)d-=12;
  simWheelPosition=pos;
  // Reject implausibly large teleporting steps on the angle slider.
  if(d>3 || d< -3)return Key::None;
  return d>0 ? Key::Down : (d<0 ? Key::Up : Key::None);
#endif
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
    float amplitude=(float)base[i]-(float)raw[i]; // original ESP32: touching lowers raw
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
  if(hold!=nowHold){hold=nowHold;keyLast=keyStable=Key::None;wheelTouch=false;return Key::HoldChanged;}
  if(hold)return Key::None;
  const Key b=readButtons();
  if(b!=keyLast){keyLast=b;changed=millis();}
  if(millis()-changed>=35 && keyStable!=keyLast){
    keyStable=keyLast;pressed=lastRepeat=millis();
    if(keyStable!=Key::None)return keyStable;
  }
  if(keyStable==Key::Previous || keyStable==Key::Next){
    if(millis()-pressed>600 && millis()-lastRepeat>160){lastRepeat=millis();return keyStable;}
  }
  return readWheel();
}
