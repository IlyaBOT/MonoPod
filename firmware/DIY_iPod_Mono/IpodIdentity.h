#pragma once
#include "Config.h"
#include <Arduino.h>

// Stable 64-bit *MonoPod* device identity, not an Apple serial-number certificate.
// Format: 4D50 (ASCII "MP") + 48-bit factory MAC. Identical across firmware updates.
namespace IpodIdentity {
  void begin();
  const char* guid();               // exactly 16 uppercase hexadecimal digits
  bool fromEfuse();                 // false => persisted random NVS fallback
}
