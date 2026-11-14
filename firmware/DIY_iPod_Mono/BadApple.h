#pragma once
#include "Config.h"
#if !EXCLUDE_BAD_APPLE_EASTEREGG
#include <SD.h>
#include <AudioGeneratorMP3.h>
#include "BadAppleAudioSource.h"
class BadApplePlayer {
public:
  bool begin(const char* file="/MonoPod/badapple.bav");
  void loop();
  void end();
  bool active() const {return running;}
  String error() const {return lastError;}
private:
  enum PixelFormat:uint8_t { GRAY2=0, GRAY6=1, RGB565=2 };
  File data;
  BadAppleAudioSource* audioSource=nullptr;
  AudioGeneratorMP3* audioDecoder=nullptr;
  uint32_t videoEnd=0;
  const uint8_t* embedded=nullptr;
  uint32_t embeddedOffset=0,embeddedLength=0;
  int nextByte();
  uint16_t next16();
  uint32_t next32();
  uint32_t length();
  uint32_t position();
  uint8_t* pixels=nullptr; // PSRAM preferred; never a large local stack buffer
  size_t frameBytes=0;
  PixelFormat pixelFormat=GRAY2;
  uint16_t fps=15; // stream-defined FPS, NOT a global 15/20/25 cap
  uint32_t totalFrames=0,frame=0,startMs=0;
  uint32_t fpsLogAt=0, fpsLogFrames=0;
  uint64_t totalDrawUs=0;
  bool running=false;
  String lastError;
  bool decodeNext();
  bool startAudio(const char* path,uint32_t offset,uint32_t size);
  void drawFrame();
};
extern BadApplePlayer badApple;
#endif
