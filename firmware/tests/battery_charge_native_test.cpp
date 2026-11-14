#include <cassert>
#include <cmath>
#include <cstdio>
#include "Battery.h"
#include "Config.h"
MockSerial Serial0;
uint32_t mockMs=0,mockAdcMilliVolts=2100;
int mockOutput=0,mockPwmDuty=0,mockPin=0;
bool mockPwmActive=false;
int mockAttachCount=0,mockDetachCount=0;
static void tick(BatteryMonitor& b,uint32_t mv){
  mockAdcMilliVolts=mv;mockMs+=Config::BATTERY_SAMPLE_INTERVAL_MS;b.loop();
}
int main(){
  static_assert(Config::BATTERY_CHARGE_ADC_ON_MV==2150,"Charging threshold must be 2150mV");
  BatteryMonitor b;
  b.begin();
  assert(b.adcMillivolts()==2100);
  assert(!b.charging());
  assert(b.level()==100);
  assert(std::fabs(b.voltage()-4.23f)<0.01f);
  tick(b,2150);assert(!b.charging()); // strict > 2.15 V
  tick(b,2151);assert(b.charging());
  tick(b,2140);assert(b.charging()); // hysteresis
  tick(b,2129);assert(!b.charging());
  tick(b,2100);assert(!b.charging());assert(b.level()==100);
  // The input 2410mV is compatible with a real 4.23V cell only if the
  // divider differs from the configured 100k/100k calibration. Don't report
  // the *inferred* 4.85V as a measured battery or mark it charging.
  tick(b,2410);assert(!b.validVoltage());assert(b.level()==-1);assert(!b.charging());
  std::puts("PASS 0.10.6.1: 4.23V ADC calibration; >2150mV enters charge; hysteresis");
}
