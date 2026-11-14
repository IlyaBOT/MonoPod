#pragma once
#include <stdint.h>
// Pure functions shared by firmware and native unit tests.
namespace BacklightMath {
inline uint16_t ease(uint32_t elapsed, uint32_t duration) {
  if (duration == 0 || elapsed >= duration) return 1024;
  const uint32_t t = (elapsed * 1024ULL) / duration;
  // One final division: intermediate truncation makes a fade non-monotonic.
  const uint64_t numerator = (uint64_t)t*t*(3072 - 2*t);
  return (uint16_t)((numerator + 524288ULL) / 1048576ULL); // smoothstep
}
inline uint16_t gammaDuty(uint8_t percent) {
  if(percent >= 100) return 1023;
  return ((uint32_t)percent * percent * 1023 + 5000) / 10000;
}
inline uint8_t mix(uint8_t start, uint8_t end, uint16_t eased) {
  const int32_t v = (int32_t)start + ((int32_t)end-(int32_t)start) * eased / 1024;
  return (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v);
}
}
