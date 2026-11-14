#pragma once
// Dependency-free circular interpolation of an already weighted six-pad
// capacitive angle. Unit-tested as native C++ without ESP32 hardware.
#include <math.h>
class WheelDetents {
public:
  void reset(){filteredAngle_=0.0f;fraction_=0.0f;}
  void anchor(float radians){filteredAngle_=radians;}
  // -1 = counter-clockwise step, +1 = clockwise, 0 = fractional movement.
  int sample(float radians){
    float delta=radians-filteredAngle_;
    while(delta>WHEEL_PI)delta-=WHEEL_TWO_PI;
    while(delta< -WHEEL_PI)delta+=WHEEL_TWO_PI;
    if(fabsf(delta)>1.8f){
      anchor(radians);fraction_=0.0f;return 0;
    }
    // Angular low pass: preserving the fractional residue lets a slow finger
    // traverse several sub-detent samples without losing any distance.
    const float filtered=delta*0.55f;
    filteredAngle_+=filtered;
    while(filteredAngle_>WHEEL_PI)filteredAngle_-=WHEEL_TWO_PI;
    while(filteredAngle_< -WHEEL_PI)filteredAngle_+=WHEEL_TWO_PI;
    fraction_+=filtered;
    if(fraction_>=DETENT){fraction_-=DETENT;return +1;}
    if(fraction_<=-DETENT){fraction_+=DETENT;return -1;}
    return 0;
  }
  static constexpr float DETENT=0.92f;
private:
  static constexpr float WHEEL_PI=3.14159265358979323846f;
  static constexpr float WHEEL_TWO_PI=6.28318530717958647692f;
  float filteredAngle_=0.0f;
  float fraction_=0.0f;
};
