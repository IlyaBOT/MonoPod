#pragma once
#include "Config.h"
#include "Types.h"
#include <SPI.h>
#include <Adafruit_GFX.h>
#if WOKWI_SIMULATION
#include <Adafruit_ILI9341.h>
using MonoDisplayDriver=Adafruit_ILI9341;
#else
#include <Adafruit_ST7735.h>
using MonoDisplayDriver=Adafruit_ST7735;
#endif
class Display {
public:
  Display();
  void begin();
  void palette(uint8_t palette);
  void clear();
  void header(const String &title, bool held,bool playing, bool wifi);
  void footer(const String &message="");
  void listRow(uint8_t row,const String &label,bool selected,bool submenu);
  void text(int x,int y,String value,uint16_t fg,uint16_t bg,uint8_t maxChars=0);
  void fillRect(int x,int y,int w,int h,uint16_t color){tft.fillRect(x+OX,y+OY,w,h,color);}
  void progress(int x,int y,int w,int percent);
  void clock(const String& datetime);
  void nowPlaying(const String&title,const String&artist,bool paused,int progress,uint32_t seconds,uint8_t volume);
  uint16_t background()const{return bg;}
  uint16_t foreground()const{return fg;}
  uint16_t selectedBackground()const{return hi;}
  uint16_t muted()const{return low;}
private:
  MonoDisplayDriver tft;
  static constexpr int OX = WOKWI_SIMULATION ? 40 : 0;
  static constexpr int OY = WOKWI_SIMULATION ? 96 : 0;
  uint16_t bg=0xCE59,fg=0x2104,hi=0x2945,low=0x8C71;
  void glyph(int x,int y,uint32_t cp,uint16_t f,uint16_t b);
};
extern Display display;
