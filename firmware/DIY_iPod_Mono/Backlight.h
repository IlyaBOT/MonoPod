#pragma once
#include <Arduino.h>
class BacklightController {
public:
  void begin(uint8_t brightnessPercent);
  void setBrightness(uint8_t brightnessPercent);
  void wake();
  void sleep();
  void loop();
  bool enabled() const { return ready; }
  bool lit() const { return desiredOn; }
private:
  bool ready=false, desiredOn=false, moving=false;
  uint8_t configured=75, from=0, current=0, target=0;
  uint32_t began=0, lastStep=0;
  uint16_t fadeMs=250;
  void request(bool on);
  void write(uint8_t percent);
};
extern BacklightController backlight;
