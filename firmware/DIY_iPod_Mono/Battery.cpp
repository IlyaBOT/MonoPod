#include "Battery.h"
#include "Config.h"
BatteryMonitor battery;
void BatteryMonitor::begin(){
#if FEATURE_BATTERY_ADC
  pinMode(Config::PIN_BATTERY_ADC,INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(Config::PIN_BATTERY_ADC, ADC_11db);
  sample();
#elif WOKWI_SIMULATION
  percent=constrain(WOKWI_BATTERY_DEMO_PERCENT,0,100); // UI-only demo: no battery simulation
#endif
}
void BatteryMonitor::loop(){
#if FEATURE_BATTERY_ADC
  if(millis()-sampledAt>=2000)sample();
#endif
}
void BatteryMonitor::sample(){
#if FEATURE_BATTERY_ADC
  sampledAt=millis();
  // High-impedance 100k/100k divider: 100nF GPIO4->GND is required for ADC sample/hold.
  // Reapply channel attenuation and discard an initial conversion. No fabricated scale factor.
  analogSetPinAttenuation(Config::PIN_BATTERY_ADC, ADC_11db);
  (void)analogReadMilliVolts(Config::PIN_BATTERY_ADC);
  uint32_t total=0;
  for(int i=0;i<12;++i){
    total+=analogReadMilliVolts(Config::PIN_BATTERY_ADC);
    delayMicroseconds(120);
  }
  // ADC input = Vbat * 100k/(100k+100k).  Real resistor tolerances need calibration.
  volts=(total/12.0f)*2.0f/1000.0f;
  if(volts<2.90f || volts>4.30f){percent=-1;return;}
  // Rough rested Li-ion voltage-to-SoC curve. Under load/charging, percentage drifts.
  static const float V[]={3.00f,3.30f,3.50f,3.60f,3.70f,3.75f,3.80f,3.85f,3.92f,4.00f,4.10f,4.20f};
  static const uint8_t P[]={0,3,7,12,20,30,40,52,65,78,90,100};
  if(volts<=V[0]){percent=0;return;}
  if(volts>=V[11]){percent=100;return;}
  for(int i=1;i<12;i++)if(volts<=V[i]){
    percent=P[i-1]+(int)((volts-V[i-1])*(P[i]-P[i-1])/(V[i]-V[i-1])+0.5f);
    return;
  }
#endif
}
