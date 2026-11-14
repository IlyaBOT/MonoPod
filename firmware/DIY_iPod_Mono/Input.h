#pragma once
#include "Types.h"
#include "Config.h"
#include "WheelDetents.h"
#include "ButtonAdcCompensation.h"
class InputManager {
public:
  void begin();
  void resetState();
  Key poll();
  bool held() const {return hold;}
  // Automatic startup/bad-baseline calibration remains internal to the input subsystem.
  bool lastEventFromWheel() const {return eventFromWheel;}
private:
  bool hold=false;
#if MONOPOD_BOARD == MONOPOD_BOARD_ESP32_S3
  ButtonAdcCompensation::Ladder buttonSupplyCompensation;
#endif
  Key keyStable=Key::None,keyLast=Key::None;
  uint32_t changed=0,pressed=0,lastRepeat=0,lastWheel=0;
  bool selectLongSent=false;
  // Prevent boot/HOLD release from interpreting an already-held or floating ADC as a key.
  bool awaitingIdle=true;
  uint32_t idleSince=0;
  uint32_t raw[6]={},base[6]={};
  uint8_t badBaselineSamples=0;
  bool wheelTouch=false, touchPresent=false, touchValid=false, eventFromWheel=false;
  uint32_t touchCandidateSince=0, touchMissingSince=0;
  float touchSum=0,touchVector=0;
  WheelDetents wheelIntegrator;
  float touchX=0,touchY=0;
  static constexpr uint8_t TOUCH_SENS_PCT=3;
  static constexpr uint8_t TOUCH_TOTAL_PCT=2;
  static constexpr uint8_t TOUCH_DOM_PCT=45;
  static constexpr uint32_t TOUCH_MIN_DELTA=5;
  uint16_t buttonAdcMillivolts();
  Key decodeButtonMillivolts(uint16_t mV) const;
  Key readButtons();
  Key readWheel();
  void touchCalibrate();
  void touchSample();
  int32_t wheelDelta(uint8_t i) const;
  void touchResetTracking();
};
extern InputManager input;
