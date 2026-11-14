#pragma once
#include "Config.h"

// ESP32-S3 prototype: no power-domain switch, deep sleep using HOLD GPIO21.
// Battery warning uses a 3V3 *proxy* on the released GPIO9 button ladder.
// It is NOT a replacement for a correctly regulated 3.3V supply.
namespace PowerGuard {
  // Must be called before settings, SD, display, Wi-Fi, audio, and touch init.
  void earlyBoot();
  // Periodically check rail only when all keys are released.
  void loop();
  // Fail-closed preflight for OTA; verifies the released GPIO9 rail proxy.
  bool voltageSafeForOta();
}
