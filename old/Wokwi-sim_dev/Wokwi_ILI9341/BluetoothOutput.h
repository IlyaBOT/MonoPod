#pragma once
// Optional A2DP audio output. Enabled by FEATURE_BLUETOOTH=1 in Config.h.
// IMPORTANT: A2DP SBC requires 44.1 kHz stereo PCM. The first revision
// does not resample 48kHz files; use 44.1 kHz files when in BT mode.
#include "Config.h"
#if FEATURE_BLUETOOTH
#include <BluetoothA2DPSource.h>
#include <AudioOutput.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
class BluetoothAudioOutput: public AudioOutput {
public:
  BluetoothAudioOutput();
  bool begin() override;
  bool SetRate(int hz) override;
  bool SetChannels(int channels) override;
  bool ConsumeSample(int16_t sample[2]) override;
  bool stop() override;
  static int32_t callback(uint8_t *data,int32_t bytes);
private:
  static constexpr int CAPACITY=4096;
  static int16_t pcm[CAPACITY*2];
  static volatile uint16_t rd,wr;
  static portMUX_TYPE mux;
  static BluetoothA2DPSource device;
  static bool initialized;
};
#endif
