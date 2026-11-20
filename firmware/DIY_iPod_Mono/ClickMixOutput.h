#pragma once
#include <Arduino.h>
#include <AudioOutput.h>

// Forwards decoded PCM and can play a short UI click while playback is idle.
class ClickMixOutput : public AudioOutput {
public:
  ClickMixOutput(AudioOutput* output,uint16_t bufferedFrames);
  bool SetRate(int hz) override;
  bool SetChannels(int channels) override;
  bool SetGain(float gain) override;
  bool begin() override;
  bool ConsumeSample(int16_t sample[2]) override;
  bool stop() override; // keep transport alive across tracks
  void setTarget(AudioOutput *target,uint16_t bufferedFrames);
  void trigger(uint16_t hz,uint8_t ms);
  void cancelClick();
  void serviceIdle(uint16_t maxFrames=128);
private:
  AudioOutput *target_;
  uint16_t bufferedFrames_;
  uint16_t drainFrames_=0;
  bool started_=false;
  bool idleClickOutput_=false;
  uint32_t remaining_=0;
  uint32_t duration_=0;
  uint32_t phase_=0;
  uint32_t increment_=0;
  int32_t sampleRate_=44100;
  uint8_t channelCount_=2;
  int16_t currentClick() const;
  void consumeAccepted();
  void stopOutput();
};
