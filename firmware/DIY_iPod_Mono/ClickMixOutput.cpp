#include "ClickMixOutput.h"
#include <algorithm>

ClickMixOutput::ClickMixOutput(AudioOutput *output):target_(output) {}
bool ClickMixOutput::SetRate(int hz) {
  if(hz<=0)return false;
  sampleRate_=hz;
  return target_ && target_->SetRate(hz);
}
bool ClickMixOutput::SetChannels(int n) {
  channelCount_=(uint8_t)n;
  return target_ && target_->SetChannels(n);
}
bool ClickMixOutput::SetGain(float gain) {return target_ && target_->SetGain(gain);}
bool ClickMixOutput::begin() {
  if(started_)return true;
  started_=target_ && target_->begin();
  return started_;
}
bool ClickMixOutput::stop() {return true;} // decoder changes must not stop click playback
void ClickMixOutput::setTarget(AudioOutput *target){
  if(target_ && started_) target_->stop();
  target_=target; started_=false;
  if(target_) {target_->SetRate(sampleRate_);target_->SetChannels(channelCount_);}
}
void ClickMixOutput::trigger(uint16_t hz,uint8_t ms) {
  if(!hz || !ms)return;
  duration_=remaining_=std::max(1L,(long)((int64_t)sampleRate_*ms/1000));
  phase_=0;
  increment_=(uint32_t)(((uint64_t)hz<<32)/(uint32_t)sampleRate_);
}
void ClickMixOutput::cancelClick(){
  remaining_=0; duration_=0; phase_=0;
}
int16_t ClickMixOutput::currentClick() const {
  if(!remaining_ || !duration_)return 0;
  // Short, deterministic broadband tick with a decaying envelope (no pitched tone).
  // A tiny unipolar pulse at 3.3 kHz sounded like a beep; this does not.
  const uint32_t elapsed=duration_-remaining_;
  uint32_t noise=(phase_ ^ (phase_>>13) ^ (elapsed*0x9E3779B9UL));
  noise^=noise<<13;noise^=noise>>17;noise^=noise<<5;
  const int32_t amp=(int32_t)(remaining_*2600ULL/duration_);
  return (int16_t)((int32_t)((noise>>16)&0xffff)-32768)*amp/32768;
}
void ClickMixOutput::consumeAccepted(){
  if(remaining_){--remaining_;phase_+=increment_;}
}
bool ClickMixOutput::ConsumeSample(int16_t sample[2]) {
  if(!begin())return false;
  int32_t click=currentClick();
  int16_t mixed[2];
  for(int i=0;i<2;i++){
    int32_t sum=(int32_t)sample[i]+click;
    if(sum>32767)sum=32767;
    if(sum< -32768)sum=-32768;
    mixed[i]=(int16_t)sum;
  }
  if(!target_->ConsumeSample(mixed))return false;
  consumeAccepted();
  return true;
}
void ClickMixOutput::serviceIdle(uint16_t maxFrames){
  if(!remaining_){
    if(started_ && target_){
      // Drain stale DMA samples then shut down idle I2S; a click must not loop.
      int16_t zero[2]={0,0};
      for(int i=0;i<256;i++)if(!target_->ConsumeSample(zero))break;
      target_->stop();started_=false;
    }
    return;
  }
  if(!begin())return;
  for(uint16_t i=0;i<maxFrames && remaining_;i++){
    int16_t silence[2]={0,0};
    if(!ConsumeSample(silence))break;
  }
}
