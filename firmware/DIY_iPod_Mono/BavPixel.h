#pragma once
#include <stddef.h>
#include <stdint.h>
// Helpers kept independent of Arduino to permit native regression tests.
namespace BavPixel {
constexpr uint8_t GRAY6=1, RGB565=2;
constexpr size_t WIDTH=160, HEIGHT=128;
constexpr size_t GRAY6_BYTES=WIDTH*HEIGHT*6/8;
constexpr size_t RGB565_BYTES=WIDTH*HEIGHT*2;
inline uint8_t gray6(const uint8_t* bytes,size_t pixel){
  const size_t at=(pixel>>2)*3;
  const uint32_t block=((uint32_t)bytes[at]<<16)|((uint32_t)bytes[at+1]<<8)|bytes[at+2];
  return (uint8_t)((block>>(18-6*(pixel&3u)))&63u);
}
inline uint16_t gray6To565(uint8_t brightness){
  const uint16_t rb=((uint32_t)brightness*31u+31u)/63u;
  return (uint16_t)((rb<<11)|((uint16_t)brightness<<5)|rb);
}
inline uint16_t rgb565(const uint8_t* littleEndian,size_t pixel){
  return (uint16_t)littleEndian[2*pixel] | ((uint16_t)littleEndian[2*pixel+1]<<8);
}
}
