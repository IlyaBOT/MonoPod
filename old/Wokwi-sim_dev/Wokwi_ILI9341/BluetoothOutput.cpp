#include "BluetoothOutput.h"
#if FEATURE_BLUETOOTH
#include <cstring>
int16_t BluetoothAudioOutput::pcm[CAPACITY*2]={};
volatile uint16_t BluetoothAudioOutput::rd=0, BluetoothAudioOutput::wr=0;
portMUX_TYPE BluetoothAudioOutput::mux=portMUX_INITIALIZER_UNLOCKED;
BluetoothA2DPSource BluetoothAudioOutput::device;
bool BluetoothAudioOutput::initialized=false;
BluetoothAudioOutput::BluetoothAudioOutput(){hertz=44100;channels=2;SetGain(.1f);}
bool BluetoothAudioOutput::begin(){
  portENTER_CRITICAL(&mux);rd=wr=0;portEXIT_CRITICAL(&mux);
  if(!initialized){device.set_data_callback(callback);device.start(Config::BT_TARGET_NAME);initialized=true;}
  return true;
}
bool BluetoothAudioOutput::SetRate(int hz){hertz=hz;return hz==44100;}
bool BluetoothAudioOutput::SetChannels(int channels){this->channels=channels;return channels==1||channels==2;}
bool BluetoothAudioOutput::ConsumeSample(int16_t sample[2]){
  portENTER_CRITICAL(&mux);
  uint16_t next=(wr+1)%CAPACITY;
  if(next==rd){portEXIT_CRITICAL(&mux);return false;}
  pcm[wr*2]=Amplify(sample[0]);pcm[wr*2+1]=Amplify(channels==1?sample[0]:sample[1]);
  wr=next;portEXIT_CRITICAL(&mux);return true;
}
int32_t BluetoothAudioOutput::callback(uint8_t *out,int32_t bytes){
  if(!out||bytes<0)return 0;
  const int32_t frames=bytes/4;
  int16_t* target=(int16_t*)out;
  for(int32_t i=0;i<frames;i++){
    portENTER_CRITICAL(&mux);
    if(rd!=wr){target[2*i]=pcm[rd*2];target[2*i+1]=pcm[rd*2+1];rd=(rd+1)%CAPACITY;}
    else {target[2*i]=0;target[2*i+1]=0;}
    portEXIT_CRITICAL(&mux);
  }
  return frames*4;
}
bool BluetoothAudioOutput::stop(){
  // Do not destroy Classic BT stack when changing tracks; it is reused.
  portENTER_CRITICAL(&mux);rd=wr=0;portEXIT_CRITICAL(&mux);
  return true;
}
#endif
