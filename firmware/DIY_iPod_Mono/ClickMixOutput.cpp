#include "ClickMixOutput.h"
#include <algorithm>

ClickMixOutput::ClickMixOutput(AudioOutput *output,uint16_t bufferedFrames)
  :target_(output),bufferedFrames_(bufferedFrames) {}
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
void ClickMixOutput::stopOutput(){
  if(started_ && target_)target_->stop();
  started_=false;
  idleClickOutput_=false;
}
void ClickMixOutput::setTarget(AudioOutput *target,uint16_t bufferedFrames){
  cancelClick();
  stopOutput();
  target_=target;
  bufferedFrames_=bufferedFrames;
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
  drainFrames_=0;
  if(idleClickOutput_)stopOutput();
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
  // Starting or resuming a track discards any unfinished idle click and
  // clears its queued audio before the first decoded PCM sample.
  if(remaining_ || idleClickOutput_)cancelClick();
  return begin() && target_->ConsumeSample(sample);
}
void ClickMixOutput::serviceIdle(uint16_t maxFrames){
  if(!remaining_ && !drainFrames_){stopOutput();return;}
  if(!begin())return;
  uint16_t sent=0;
  for(;sent<maxFrames && remaining_;sent++){
    const int16_t click=currentClick();
    int16_t sample[2]={click,click};
    if(!target_->ConsumeSample(sample))return;
    idleClickOutput_=true;
    consumeAccepted();
    if(!remaining_)drainFrames_=bufferedFrames_;
  }
  // A full buffer of accepted silence pushes the tick through the transport.
  // Count only accepted frames so retrying a full DMA buffer stays nonblocking.
  int16_t silence[2]={0,0};
  for(;sent<maxFrames && drainFrames_;sent++){
    if(!target_->ConsumeSample(silence))return;
    --drainFrames_;
  }
  if(!remaining_ && !drainFrames_)stopOutput();
}
