#pragma once
#include "Config.h"
#include "Types.h"
#if FEATURE_PIEZO
#include <atomic>
#include <esp_timer.h>
#endif

class FeedbackController {
public:
  void begin();
  void loop();
  void quiet();
  void setRuntimeMuted(bool muted);
  bool runtimeMuted() const {return suppressed;}
  uint32_t eventCount() const {return fired;}
  void cursorMoved(uint32_t steps=1);
  void setMode(ClickerMode route);
  const char* modeName(ClickerMode mode) const;
  bool available(ClickerMode mode) const;
private:
  ClickerMode selected=ClickerMode::Off;
  bool localReady=false;
  bool suppressed=false;
  uint32_t fired=0;
#if FEATURE_PIEZO
  esp_timer_handle_t pulseTimer=nullptr;
  uint32_t pendingPulses=0;
  std::atomic<bool> pulseActive{false};
  uint32_t lastPulseEnd=0;
  static void endPulse(void* arg);
#endif
};
extern FeedbackController feedback;
