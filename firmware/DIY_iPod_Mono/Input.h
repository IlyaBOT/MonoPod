#pragma once
#include "Types.h"
#include "Config.h"
class InputManager {
public:
  void begin();
  void resetState();
  Key poll();
  bool held() const {return hold;}
  // Touch diagnostics: runtime-only controls, preserved until reboot.
  // Lower SENS or TOTAL means more sensitive; higher values reject more noise.
  void touchCalibrate(); // release the wheel before calling
  void touchSample(); // read raw electrodes without generating key events
  void touchEnable(bool enabled);
  bool touchEnabled() const {return touchOn;}
  void touchSetSensitivity(uint8_t percent);
  void touchSetTotalThreshold(uint8_t percent);
  void touchSetMinimumDelta(uint32_t rawUnits);
  void touchSetDominance(uint8_t percent);
  bool touchSetPadSensitivity(uint8_t channel, uint8_t percent);
  uint8_t touchPadSensitivity(uint8_t channel) const {return channel<Config::WHEEL_COUNT?touchPadPct[channel]:0;}
  uint8_t touchSensitivity() const {return touchSensitivityPct;}
  uint8_t touchTotalThreshold() const {return touchTotalPct;}
  uint32_t touchMinimumDelta() const {return touchMinDelta;}
  uint8_t touchDominance() const {return touchDominancePct;}
  uint32_t wheelRaw(uint8_t i) const {return i<Config::WHEEL_COUNT?raw[i]:0;}
  uint32_t wheelBase(uint8_t i) const {return i<Config::WHEEL_COUNT?base[i]:0;}
  int32_t wheelDelta(uint8_t i) const;
  bool touchDetected() const {return touchPresent;}
  bool touchTracking() const {return wheelTouch;}
  uint8_t touchPeakChannel() const {return peakChannel;}
  uint32_t touchLastSampleMs() const {return touchLastSample;}
  float touchWeightSum() const {return touchSum;}
  float touchVectorLength() const {return touchVector;}
  bool touchSampleValid() const {return touchValid;}
  void touchResetTracking();
  uint16_t buttonAdcMillivolts(); // median of 3 readings, physical ADC GPIO9
  bool buttonsReady() const {return !awaitingIdle;}
  Key decodeButtonMillivolts(uint16_t mv) const;
private:
  bool hold=false;
  Key keyStable=Key::None,keyLast=Key::None;
  uint32_t changed=0,pressed=0,lastRepeat=0,lastWheel=0;
  bool selectLongSent=false;
  // Prevent boot/HOLD release from interpreting an already-held or floating ADC as a key.
  bool awaitingIdle=true;
  uint32_t idleSince=0;
  uint32_t raw[6]={},base[6]={};
  // Updated only after a stable touch. Sampling for UART never moves the cursor.
  bool wheelTouch=false, touchPresent=false, touchValid=false, touchOn=true;
  uint32_t touchCandidateSince=0, touchLastSample=0;
  uint8_t peakChannel=0;
  float lastAngle=0,angleDelta=0,touchSum=0,touchVector=0;
  float touchX=0,touchY=0;
  uint8_t touchSensitivityPct=10, touchTotalPct=10, touchDominancePct=50;
  uint8_t touchPadPct[6]={}; // 0: use global sensitivity
  uint32_t touchMinDelta=5;
  Key readButtons();
  Key readWheel();
};
extern InputManager input;
