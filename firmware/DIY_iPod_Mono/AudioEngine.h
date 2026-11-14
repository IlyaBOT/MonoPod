#pragma once
#include "Types.h"
#include <vector>
#include <AudioGenerator.h>
#include <AudioFileSourceSD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <AudioOutput.h>
#include <AudioOutputI2S.h>
#include "ClickMixOutput.h"
#if FEATURE_FLAC
#include "ChunkedFlacSource.h"
#endif
class AudioEngine {
public:
  bool begin();
  void setVolume(uint8_t v);
  void headphoneClick(uint16_t freq,uint8_t durationMs);
  void stopHeadphoneClick();
  // Exclusive external MP3 playback during Bad Apple (main decoder stopped).
  AudioOutput* externalAudioSink() const;
  bool play(size_t libraryIndex,const std::vector<size_t>& context);
  void stop();
  void pauseToggle();
  void next(bool userInitiated=true);
  void previous();
  void loop();
  void suspendBackground(bool suspend);  // exclude I2S use by BadApple/Sync mode
  bool backgroundSuspended() const;
  bool active() const;
  bool paused() const;
  int current() const;
  uint32_t elapsedSeconds() const;
  uint32_t durationSeconds() const;
  uint32_t remainingSeconds() const;
  uint32_t ordinal() const;
  uint32_t queueSize() const;
  bool seekPercent(uint8_t percent); // legacy percent interface
  bool seekSeconds(uint32_t seconds); // second-resolution seek, duration must be known
  uint8_t approximateProgress() const;
#if FEATURE_BLUETOOTH
  bool bluetooth() const {return bluetoothOn;}
  bool switchBluetooth(bool on);
#endif
  String errorText() const;
private:
  // Lock all player operations from loopTask and the core-0 audio worker.
  // Recursive because play/next/seek call other locked helpers.
  mutable SemaphoreHandle_t mutex=nullptr;
  friend class AudioEngineGuard;
  volatile bool backgroundSuspended_=false;
  AudioFileSourceSD *source=nullptr;
#if FEATURE_FLAC
  ChunkedFlacSource *flacBuffer=nullptr;
#endif
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
  uint32_t playStarted=0,pauseStarted=0,pausedMs=0,seekBaseSeconds=0;
  String error;
  bool startFile(size_t index);
  void releaseDecoder();
};
extern AudioEngine player;
