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
  void palette(uint8_t p);
  bool reloadTheme();
  bool themeLoaded() const;
  void clear();
  void header(const String& title,bool held,bool playing,uint8_t wifiBars);
  void wifiSignal(uint8_t bars); // refresh only the icon region; 0 hides it
  void footer(const String& message=""); // Explicit info only; no generic item counter
  void listRow(uint8_t row,const String& label,bool selected,bool submenu);
  void text(int x,int y,const String& value,uint16_t front,uint16_t back,uint8_t maxChars=0);
  void smallText(int x,int y,const String& value,uint16_t front,uint16_t back,uint8_t maxChars=0);
  void centerText(int y,const String& value,bool small=false,bool inverted=false);
  void fillRect(int x,int y,int w,int h,uint16_t color){tft.fillRect(x+OX,y+OY,w,h,color);}
  void progress(int x,int y,int w,int percent);
  void clock(const String& dt);
  void syncScreen(bool wifi, bool transferring);
  void nowPlaying(const String& title,const String& artist,bool paused,int progress,uint32_t seconds,uint8_t volume);
  uint16_t background()const{return bg;}
  uint16_t foreground()const{return fg;}
  uint16_t selectedBackground()const{return hi;}
  uint16_t muted()const{return low;}
  int textWidth(const String& s,bool small=false)const;
  static constexpr uint8_t ROW_COUNT=5;
private:
  MonoDisplayDriver tft;
  static constexpr int OX=WOKWI_SIMULATION?40:0;
  static constexpr int OY=WOKWI_SIMULATION?96:0;
  uint16_t bg=0xCE59,fg=0x2104,hi=0x2945,low=0x8C71;
  void drawGlyph(int x,int y,uint32_t cp,uint16_t front,uint16_t back,bool small);
  void drawString(int x,int y,const String& s,uint16_t front,uint16_t back,bool small,uint8_t maxChars,int maxX=160);
};
extern Display display;
