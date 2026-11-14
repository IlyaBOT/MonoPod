#include "../DIY_iPod_Mono/BavPixel.h"
#include <cassert>
#include <cstdint>
int main() {
  static_assert(BavPixel::GRAY6_BYTES==15360,"bad gray buffer");
  static_assert(BavPixel::RGB565_BYTES==40960,"bad RGB buffer");
  const uint8_t bytes[]={0x01,0x53,0xFF};
  assert(BavPixel::gray6(bytes,0)==0);
  assert(BavPixel::gray6(bytes,1)==21);
  assert(BavPixel::gray6(bytes,2)==15); // third value of 0x0153ff
  assert(BavPixel::gray6(bytes,3)==63);
  assert(BavPixel::gray6To565(0)==0);
  assert(BavPixel::gray6To565(63)==0xffff);
  const uint8_t rgb[]={0x00,0xf8,0xe0,0x07,0x1f,0x00};
  assert(BavPixel::rgb565(rgb,0)==0xf800); // red
  assert(BavPixel::rgb565(rgb,1)==0x07e0); // green
  assert(BavPixel::rgb565(rgb,2)==0x001f); // blue
}
