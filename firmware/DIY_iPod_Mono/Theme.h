#pragma once
#include <Arduino.h>
// MonoPod Theme Format MPT1, little endian. See Studio/theme_format.py.
// A user-generated theme is optional and stored at /MonoPod/theme.mpt on microSD.
// If unavailable or invalid, the built-in 1-bit font remains usable.
class ThemeManager {
public:
  struct Glyph {uint32_t codepoint; uint32_t offset; uint8_t width; uint8_t advance;};
  ThemeManager() = default;
  ~ThemeManager();
  static bool validate(const char* path);
  bool reload(const char* path="/MonoPod/theme.mpt");
  void unload();
  bool active() const {return bytes != nullptr;}
  bool glyph(uint32_t codepoint,bool small,Glyph& out) const;
  uint8_t alpha(const Glyph& g, bool small, uint8_t x, uint8_t y) const;
  uint16_t color(uint8_t index) const{return colors[index<12?index:1];}
  static constexpr uint32_t MAX_BYTES = 131072;
private:
  uint8_t *bytes=nullptr;
  uint32_t length=0;
  Glyph big[256]={},tiny[256]={};
  uint16_t bigCount=0,smallCount=0;
  uint8_t bigHeight=16,smallHeight=12;
  uint16_t colors[12]={0xce59,0x2104,0x2945,0x8c71};
};
extern ThemeManager theme;
