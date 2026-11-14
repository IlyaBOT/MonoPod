#pragma once
#include <stdint.h>

// Supply-independent interpretation of the five-button 20kOhm ADC ladder.
// Measured S3 values at nominal 3.3V: 0, 655, 1075, 1650, 2215 mV.
// At a sagging 2.9V rail these all drop proportionally; the ADC uses an
// INTERNAL reference, so absolute analogReadMilliVolts thresholds do not.
//
// The single GPIO9 can measure the rail only when all buttons are released.
// In that state the external pull-up connects GPIO9 directly to 3V3. The
// ESP32-S3 11dB ADC saturates near 3.1V, so the observed idle voltage is an
// *ADC reference for a ratio*, not a precision 3V3 rail voltmeter.
//
// This module is plain C++ so it can be tested without Arduino hardware.
namespace ButtonAdcCompensation {

enum class Button : uint8_t { None, Menu, Previous, Next, PlayPause, Select };

class Ladder {
public:
  // Measured open GPIO9 (3.3V rail) was ~3148mV. Start here until a released
  // sample becomes available. The supply estimate is not stored across boots.
  static constexpr uint16_t kInitialIdleMv = 3150;
  // At valid operating rail voltages >=3.0V, the SELECT level is below 2.5V.
  // Use this threshold to distinguish released GPIO9 from actual key presses.
  static constexpr uint16_t kIdleMinMv = 2450;
  static constexpr uint16_t kIdleMaxMv = 3300;

  void reset() { idleMv_ = kInitialIdleMv; idleSeen_ = false; }

  // Call on each ADC sample, before decoding. While a button is held, the
  // rail can't be measured through this pin; keep the last known reference.
  void observe(uint16_t adcMv) {
    if (adcMv < kIdleMinMv || adcMv > kIdleMaxMv) return;
    if (!idleSeen_) {
      idleMv_ = adcMv;
      idleSeen_ = true;
    } else {
      // One-pole smoothing suppresses noise and occasional release glitches.
      // 1/4 gives rapid tracking if the battery rail drifts under load.
      const int32_t difference = (int32_t)adcMv - (int32_t)idleMv_;
      idleMv_ = (uint16_t)((int32_t)idleMv_ + difference / 4);
    }
  }

  Button decode(uint16_t adcMv) const {
    // The open-circuit level must NEVER be mistaken for SELECT. An unexpected
    // high ADC reading also becomes None (including the measured ~3148mV).
    if (adcMv >= kIdleMinMv) return Button::None;
    const uint16_t perMille = (uint16_t)(((uint32_t)adcMv * 1000u + idleMv_ / 2u) / idleMv_);
    if (perMille <= 45) return Button::Menu;
    if (perMille >= 155 && perMille <= 265) return Button::Previous;
    if (perMille >= 285 && perMille <= 410) return Button::Next;
    if (perMille >= 440 && perMille <= 575) return Button::PlayPause;
    if (perMille >= 590 && perMille <= 765) return Button::Select;
    // Deliberate dead zones; debounce deals with transitions between levels.
    return Button::None;
  }

  uint16_t referenceMilliVolts() const { return idleMv_; }
  bool hasIdleSample() const { return idleSeen_; }

private:
  uint16_t idleMv_ = kInitialIdleMv;
  bool idleSeen_ = false;
};

}  // namespace ButtonAdcCompensation
