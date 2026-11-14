#include "../DIY_iPod_Mono/MarqueeMotion.h"
#include <cassert>
#include <cstdio>
#include <stdint.h>
#include <initializer_list>
int main(){
  using namespace MarqueeMotion;
  assert(pixelOffset(1000,1000)==0);
  assert(pixelOffset(1000,1499)==0);
  assert(pixelOffset(1000,1500)==0);
  assert(pixelOffset(1000,1580)==1);
  assert(pixelOffset(1000,1660)==2);
  // Covers slight overflow (+1-2 chars), medium and long titles.
  for(int n:{153,170,205,360}) {
    const int period=n+kGapPixels;
    assert(phase(0,n)==0);
    assert(phase(period-1,n)==period-1);
    assert(phase(period,n)==0);
    assert(phase(period+1,n)==1);
  }
  // millis() rollover: time advances 600ms across the uint32 boundary.
  assert(pixelOffset(UINT32_MAX-199,400)==1);
  puts("Marquee motion: PASS");
}
