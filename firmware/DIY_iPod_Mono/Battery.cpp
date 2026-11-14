#include "Battery.h"
#include "Config.h"
BatteryMonitor battery;
void BatteryMonitor::begin(){
#if FEATURE_BATTERY_ADC
  pinMode(Config::PIN_BATTERY_ADC,INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(Config::PIN_BATTERY_ADC, ADC_11db);
  sample();
#endif
}
void BatteryMonitor::loop(){
#if FEATURE_BATTERY_ADC
  if(millis()-sampledAt>=Config::BATTERY_SAMPLE_INTERVAL_MS)sample();
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
  // Based on a 100k/100k divider calibrated against 4.23V -> 2.10V.
  // A measured 2.41V at GPIO4 can coexist with a normal 4.23V cell if the
  // actual divider ratio differs from the configured calibration. The inferred
  // value is NOT a direct measurement of the battery; flag the mismatch.
  // Never silently turn a wrong estimate into '100%' or 'Charging'.
  adcMv=(uint16_t)((total+6u)/12u);
  volts=(total/12.0f)*Config::BATTERY_ADC_RATIO/1000.0f + Config::BATTERY_ADC_OFFSET_V;
  fault=volts>4.35f?2:(volts<2.90f?1:0);
  if(fault){
    percent=-1;
    chargeDetected=false;
    MONOPOD_LOG.printf("[BATTERY] ESTIMATE_OUT_OF_RANGE adc=%u mV computed_cell=%.3f V (NOT measured); check divider/ADC calibration against multimeter\n",
      (unsigned)adcMv,(double)volts);
    return;
  }
  // Voltage threshold is only an indicator, NOT a measurement of charging current.
  if(chargeDetected){
    if(adcMv<Config::BATTERY_CHARGE_ADC_OFF_MV)chargeDetected=false;
  }else if(adcMv>Config::BATTERY_CHARGE_ADC_ON_MV){
    chargeDetected=true;
  }
  // Rough rested Li-ion voltage-to-SoC curve. Under load/charging, percentage drifts.
  static const float V[]={3.00f,3.30f,3.50f,3.60f,3.70f,3.75f,3.80f,3.85f,3.92f,4.00f,4.10f,Config::BATTERY_FULL_VOLTAGE};
  static const uint8_t P[]={0,3,7,12,20,30,40,52,65,78,90,100};
  if(volts<=V[0]){percent=0;return;}
  if(volts>=V[11]){percent=100;return;}
  for(int i=1;i<12;i++)if(volts<=V[i]){
    percent=P[i-1]+(int)((volts-V[i-1])*(P[i]-P[i-1])/(V[i]-V[i-1])+0.5f);
    return;
  }
#endif
}
