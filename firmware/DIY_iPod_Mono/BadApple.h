#pragma once
#include "Config.h"
#if !EXCLUDE_BAD_APPLE_EASTEREGG
#include <SD.h>
class BadApplePlayer {
public:
  bool begin(const char* file="/MonoPod/badapple.bav");
  void loop();
  void end();
  bool active()const{return running;}
  String error()const{return lastError;}
private:
  File data;
  const uint8_t* embedded=nullptr;
  uint32_t embeddedOffset=0,embeddedLength=0;
  int nextByte();
  uint16_t next16();
  uint32_t next32();
  uint32_t length();
  uint32_t position();
  uint8_t pixels[5120]={}; // four grayscale levels, 2 bpp
  uint8_t fps=15;
  uint32_t totalFrames=0,frame=0,nextFrame=0;
  bool running=false;
  String lastError;
  bool decodeNext();
  void drawFrame();
};
extern BadApplePlayer badApple;
#endif
