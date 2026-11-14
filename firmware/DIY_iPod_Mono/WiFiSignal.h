#pragma once
#include <stdint.h>
// Connected stations always show one to three bars. Hidden = not connected.
namespace WiFiSignal {
inline constexpr uint8_t barsForRssi(int32_t rssi) {
  return rssi >= -55 ? 3 : (rssi >= -70 ? 2 : 1);
}
// Handles millis() rollover by using elapsed unsigned subtraction.
inline constexpr bool refreshDue(uint32_t now,uint32_t last,uint32_t period=12000) {
  return static_cast<uint32_t>(now-last)>=period;
}
}
