#pragma once
#include <stdint.h>
// Shared, wrap-safe movement model for status bar, track title and selected rows.
// No dynamic allocation; pixel positions are based on elapsed time, not frame count.
namespace MarqueeMotion {
constexpr uint32_t kStartDelayMs = 500;
constexpr uint32_t kStepIntervalMs = 80; // 12.5 FPS, one pixel per tick
constexpr int kGapPixels = 20;
inline uint32_t pixelOffset(uint32_t startedAt,uint32_t now){
  const uint32_t elapsed=now-startedAt; // unsigned millis() rollover is well-defined
  return elapsed<=kStartDelayMs?0:(elapsed-kStartDelayMs)/kStepIntervalMs;
}
inline int phase(uint32_t pixels,int textWidth){
  return textWidth>0 ? (int)(pixels%(uint32_t)(textWidth+kGapPixels)) : 0;
}
}
