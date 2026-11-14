#pragma once
#include "Types.h"
#include <vector>
#include <AudioGenerator.h>
#include <AudioFileSourceSD.h>
#include <AudioOutput.h>
#include <AudioOutputI2S.h>
#include "ClickMixOutput.h"
class AudioEngine {
public:
  bool begin();
  void setVolume(uint8_t v);
  void headphoneClick(uint16_t freq,uint8_t durationMs);
  void stopHeadphoneClick();
  bool play(size_t libraryIndex,const std::vector<size_t>& context);
  void stop();
  void pauseToggle();
  void next(bool userInitiated=true);
  void previous();
  void loop();
  bool active() const {return activeTrack>=0;}
  bool paused() const {return pausedState;}
  int current() const {return activeTrack;}
  uint32_t elapsedSeconds() const;
  uint8_t approximateProgress() const;
#if FEATURE_BLUETOOTH
  bool bluetooth() const {return bluetoothOn;}
  bool switchBluetooth(bool on);
#endif
  String errorText() const {return error;}
private:
  AudioFileSourceSD *source=nullptr;
  AudioGenerator *decoder=nullptr;
  AudioOutputI2S *i2s=nullptr;
  ClickMixOutput *clickMix=nullptr;
  AudioOutput *sink=nullptr;
  std::vector<size_t> queue;
  int queuePos=-1;
  int activeTrack=-1;
  bool pausedState=false;
#if FEATURE_BLUETOOTH
  bool bluetoothOn=false;
#endif
  uint32_t playStarted=0,pauseStarted=0,pausedMs=0;
  String error;
  bool startFile(size_t index);
  void releaseDecoder();
};
extern AudioEngine player;
