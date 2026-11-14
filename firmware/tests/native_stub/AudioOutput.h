#pragma once
#include <stdint.h>
class AudioOutput {
public:
 virtual ~AudioOutput()=default;
 virtual bool SetRate(int)=0;
 virtual bool SetChannels(int)=0;
 virtual bool SetGain(float)=0;
 virtual bool begin()=0;
 virtual bool ConsumeSample(int16_t sample[2])=0;
 virtual bool stop()=0;
};
