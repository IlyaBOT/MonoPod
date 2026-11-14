#pragma once
#include <Arduino.h>
// This is a voltage-based APPROXIMATION, not a coulomb counter.
class BatteryMonitor {
public:
  void begin();
  void loop();
  int level() const {return percent; } // 0..100, -1 unknown
  float voltage() const {return volts; }
private:
  int percent=-1;
  float volts=0;
  uint32_t sampledAt=0;
  void sample();
};
extern BatteryMonitor battery;
