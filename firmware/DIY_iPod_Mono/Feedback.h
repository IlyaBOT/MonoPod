#pragma once
#include "Config.h"
#include "Types.h"

enum class FeedbackEvent : uint8_t { Scroll, Select, Menu, Transport };

class FeedbackController {
public:
  void begin();
  void loop();
  void quiet();
  void setRuntimeMuted(bool muted);
  bool runtimeMuted() const {return suppressed;}
  uint32_t eventCount() const {return fired;}
  void fire(FeedbackEvent event);
  void setMode(ClickerMode route);
  ClickerMode nextMode(ClickerMode current) const;
  const char* modeName(ClickerMode mode) const;
  bool available(ClickerMode mode) const;
private:
  ClickerMode selected=ClickerMode::Off;
  bool localReady=false;
  bool suppressed=false;
  uint32_t fired=0;
  bool piezoPlaying=false;
  bool secondPulsePending=false;
  uint16_t secondPulseHz=0;
  uint8_t secondPulseMs=0;
  uint32_t piezoStopAt=0;
  uint32_t secondPulseAt=0;
  uint32_t lastScrollAt=0;
  uint32_t lastActionAt=0;
  void startTone(uint16_t hz,uint8_t durationMs);
  void silencePiezo();
  void localEffect(FeedbackEvent event,uint32_t now);
};
extern FeedbackController feedback;
