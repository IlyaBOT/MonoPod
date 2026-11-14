#pragma once
#include <Arduino.h>
// This is a voltage-based APPROXIMATION, not a coulomb counter.
class BatteryMonitor {
public:
  void begin();
  void loop();
  int level() const {return percent; } // 0..100, -1 unknown
  float voltage() const {return volts; }
  bool charging() const {return chargeDetected && fault==0; }
  const char* adcStatus() const {return fault==2?"ESTIMATE_HIGH_CHECK_DIVIDER":fault==1?"ESTIMATE_LOW_CHECK_DIVIDER":"OK"; }
  bool validVoltage() const {return fault==0; }
  uint16_t adcMillivolts() const {return adcMv; }
private:
  int percent=-1;
  bool chargeDetected=false;
  uint16_t adcMv=0;
  uint8_t fault=0; // 0=valid, 1=too low, 2=too high for 1S Li-ion + divider
  float volts=0;
  uint32_t sampledAt=0;
  void sample();
};
extern BatteryMonitor battery;
