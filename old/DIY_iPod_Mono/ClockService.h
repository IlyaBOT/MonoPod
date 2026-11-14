#pragma once
#include "Config.h"
#include <Arduino.h>
#ifndef FEATURE_DS3231
#define FEATURE_DS3231 0
#endif
#if FEATURE_DS3231 && FEATURE_TOUCH_WHEEL && !FEATURE_HAPTIC_DRV2605
#error "DS3231 uses I2C 32/33; six-electrode touch wheel owns those pins. Select four-sector wheel with Haptic or disable touch."
#endif
class ClockService {
public:
  void begin();
  String dateTime() const;
  bool valid() const;
  void setUnix(time_t epoch);
};
extern ClockService clockService;
