#pragma once
#include "Types.h"
#include "Config.h"
class InputManager {
public:
  void begin();
  Key poll();
  bool held() const {return hold;}
  uint16_t wheelRaw(uint8_t i) const {return raw[i];}
private:
  bool hold=false;
  Key keyStable=Key::None,keyLast=Key::None;
  uint32_t changed=0,pressed=0,lastRepeat=0,lastWheel=0;
  uint16_t raw[6]={},base[6]={};
  bool wheelTouch=false;
  float lastAngle=0,angleDelta=0;
  Key readButtons();
  Key readWheel();
#if FEATURE_DEV_ENCODER
  uint8_t encoderPrevious=0;
  int8_t encoderQuarter=0;
#endif
};
extern InputManager input;
