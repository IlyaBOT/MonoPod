#pragma once
#include <AudioGeneratorFLAC.h>
// The upstream decoder already uses libFLAC and supports seek callbacks, but
// does not expose sample-level seeking. Seeking to bytes would corrupt frames.
class SeekableFlacDecoder : public AudioGeneratorFLAC {
public:
  bool seekToSample(uint64_t sample){
    if(!flac || !running)return false;
    // Skip the previous decoded PCM block before libFLAC locates the target.
    buffPtr=buffLen=0;
    lastSample[0]=lastSample[1]=0;
    if(!FLAC__stream_decoder_seek_absolute(flac,(FLAC__uint64)sample))return false;
    // seek_absolute may decode the target frame through our write callback.
    // Its buffPtr / buffLen must NOT be reset again here.
    return true;
  }
};
