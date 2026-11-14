#pragma once
#include <stddef.h>
#include <stdint.h>

// Supported SD speeds in descending order. The card must be re-mounted
// between attempts; never change SPI clock while a filesystem is mounted.
namespace SpiSpeedPolicy {
  constexpr uint32_t supportedHz[] = {20000000UL, 16000000UL, 8000000UL, 4000000UL};
  inline size_t candidates(uint32_t requestedHz, uint32_t (&out)[4]) {
    size_t count = 0;
    for (uint32_t hz : supportedHz) {
      if (hz <= requestedHz) out[count++] = hz;
    }
    return count;
  }
}
