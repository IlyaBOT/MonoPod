#include "../DIY_iPod_Mono/SpiSpeedPolicy.h"
#include <assert.h>
#include <stdio.h>
int main() {
  uint32_t rates[4] = {};
  size_t n = SpiSpeedPolicy::candidates(16000000UL, rates);
  assert(n == 3 && rates[0] == 16000000UL && rates[1] == 8000000UL && rates[2] == 4000000UL);
  n = SpiSpeedPolicy::candidates(20000000UL, rates);
  assert(n == 4 && rates[0] == 20000000UL && rates[1] == 16000000UL && rates[2] == 8000000UL && rates[3] == 4000000UL);
  n = SpiSpeedPolicy::candidates(8000000UL, rates);
  assert(n == 2 && rates[0] == 8000000UL && rates[1] == 4000000UL);
  puts("SPI speed candidates: OK");
}
