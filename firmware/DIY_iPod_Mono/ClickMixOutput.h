#pragma once
#include <Arduino.h>
#include <AudioOutput.h>

// Takes decoded 16-bit PCM from ESP8266Audio and adds a short synthesized click.
// Only accepts a source sample after the physical AudioOutput accepts it.
// Has an idle service for audible UI clicks when no song is playing.
class ClickMixOutput : public AudioOutput {
public:
  explicit ClickMixOutput(AudioOutput* output);
  bool SetRate(int hz) override;
  bool SetChannels(int channels) override;
  bool SetGain(float gain) override;
  bool begin() override;
  bool ConsumeSample(int16_t sample[2]) override;
  bool stop() override; // keep transport alive across tracks
  void setTarget(AudioOutput *target);
  void trigger(uint16_t frequencyHz,uint8_t durationMs);
  void serviceIdle(uint16_t maxFrames=128);
private:
  AudioOutput *target_;
  bool started_=false;
  uint32_t remaining_=0;
  uint32_t duration_=0;
  uint32_t phase_=0;
  uint32_t increment_=0;
  int32_t sampleRate_=44100;
  uint8_t channelCount_=2;
  int16_t currentClick() const;
  void consumeAccepted();
};
