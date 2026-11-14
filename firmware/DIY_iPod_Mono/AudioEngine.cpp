#include "AudioEngine.h"
#include "Config.h"
#include "Library.h"
#include "Settings.h"
#include <AudioGeneratorMP3.h>
#include <AudioGeneratorWAV.h>
#if FEATURE_FLAC
#include <AudioGeneratorFLAC.h>
#endif
#if FEATURE_AAC
#include <AudioGeneratorAAC.h>
#endif
#if FEATURE_BLUETOOTH
#include "BluetoothOutput.h"
static BluetoothAudioOutput btOutput;
#endif
AudioEngine player;
bool AudioEngine::begin() {
  i2s=new AudioOutputI2S();
  if(!i2s){error="Cannot allocate I2S";return false;}
  i2s->SetPinout(Config::PIN_I2S_BCLK,Config::PIN_I2S_LRC,Config::PIN_I2S_DATA);
  clickMix=new ClickMixOutput(i2s);
  if(!clickMix){error="Cannot allocate click mixer";return false;}
  sink=clickMix;
  clickMix->SetRate(44100);
  clickMix->SetChannels(2);
  setVolume(settings.data.volume);
  // Idle UI clicks work even before the first track is started.
  return clickMix->begin();
}
void AudioEngine::headphoneClick(uint16_t frequency,uint8_t durationMs){
#if FEATURE_AUDIO
  if(clickMix) clickMix->trigger(frequency,durationMs);
#else
  (void)frequency; (void)durationMs;
#endif
}
void AudioEngine::stopHeadphoneClick(){
  if(clickMix)clickMix->cancelClick();
}
void AudioEngine::setVolume(uint8_t v){
  float gain=(constrain((int)v,0,(int)Config::MAX_VOLUME)/100.0f);
  // Square-law attenuation; safety cap applied via Config::MAX_VOLUME.
  gain=gain*gain;
  if(i2s)i2s->SetGain(gain);
#if FEATURE_BLUETOOTH
  btOutput.SetGain(gain);
#endif
}
void AudioEngine::releaseDecoder() {
  if(decoder){decoder->stop();delete decoder;decoder=nullptr;}
  if(source){source->close();delete source;source=nullptr;}
}
void AudioEngine::stop() {
  releaseDecoder();activeTrack=-1;pausedState=false;queuePos=-1;queue.clear();
}
bool AudioEngine::startFile(size_t index) {
  releaseDecoder();pausedState=false;pausedMs=0;seekBaseSeconds=0;
  if(index>=media.size())return false;
  const Track &t=media.at(index);
  source=new AudioFileSourceSD(t.path.c_str());
  if(!source || !source->isOpen()){error="Cannot open "+t.path;releaseDecoder();activeTrack=-1;return false;}
  String p=t.path;p.toLowerCase();
  if(p.endsWith(".mp3"))decoder=new AudioGeneratorMP3();
  else if(p.endsWith(".wav"))decoder=new AudioGeneratorWAV();
#if FEATURE_FLAC
  else if(p.endsWith(".flac"))decoder=new AudioGeneratorFLAC();
#endif
#if FEATURE_AAC
  else if(p.endsWith(".aac"))decoder=new AudioGeneratorAAC();
#endif
  else{error="Unsupported format";releaseDecoder();activeTrack=-1;return false;}
  if(!decoder){error="Decoder allocation failed";releaseDecoder();activeTrack=-1;return false;}
  if(!decoder->begin(source,sink)){error="Decoder begin failed";releaseDecoder();activeTrack=-1;return false;}
  activeTrack=(int)index;playStarted=millis();error="";return true;
}
bool AudioEngine::play(size_t libraryIndex,const std::vector<size_t>& context){
  if(!sink){error="Audio disabled or output not initialized";return false;}
  queue=context;
  if(queue.empty())media.allIndices(queue);
  queuePos=-1;for(size_t i=0;i<queue.size();i++)if(queue[i]==libraryIndex){queuePos=(int)i;break;}
  if(queuePos<0){queue.insert(queue.begin(),libraryIndex);queuePos=0;}
  return startFile(libraryIndex);
}
void AudioEngine::pauseToggle(){
  if(!active())return;
  if(pausedState){pausedMs+=millis()-pauseStarted;pausedState=false;}
  else{pausedState=true;pauseStarted=millis();}
}
uint32_t AudioEngine::elapsedSeconds() const {
  if(!active())return 0;
  uint32_t end=pausedState?pauseStarted:millis();
  return seekBaseSeconds+(end-playStarted-pausedMs)/1000;
}
uint32_t AudioEngine::durationSeconds() const {
  if(activeTrack<0 || (size_t)activeTrack>=media.size())return 0;
  return media.at((size_t)activeTrack).durationSeconds;
}
uint32_t AudioEngine::remainingSeconds() const {
  uint32_t duration=durationSeconds();
  uint32_t elapsed=elapsedSeconds();
  return duration>elapsed?duration-elapsed:0;
}
bool AudioEngine::seekPercent(uint8_t pct){
  if(!active() || !source || !decoder)return false;
  pct=constrain((int)pct,0,99); // keep seek away from EOF
  String path=media.at((size_t)activeTrack).path;path.toLowerCase();
  // Seeking arbitrary compressed frames requires frame sync; MPEG is experimental here.
  if(!path.endsWith(".mp3") && !path.endsWith(".wav"))return false;
  uint32_t length=source->getSize();if(length<128)return false;
  uint32_t pos=(uint32_t)(((uint64_t)length*pct)/100ULL);
  // Retain AudioEngine queue; restart decoder then seek its file source.
  int index=activeTrack;
  if(!startFile((size_t)index))return false;
  bool ok=source->seek((int32_t)pos,SEEK_SET);
  if(!ok){startFile((size_t)index);return false;}
  uint32_t duration=durationSeconds();seekBaseSeconds=duration?(uint32_t)(((uint64_t)duration*pct)/100ULL):0;
  playStarted=millis();pausedMs=0;pausedState=false;
  return true;
}
uint8_t AudioEngine::approximateProgress() const {
  if(!source || !source->getSize())return 0;
  if(durationSeconds())return (uint8_t)min(100UL,100UL*elapsedSeconds()/durationSeconds());
  return (uint8_t)min(100UL,(100UL*source->getPos())/source->getSize());
}
void AudioEngine::next(bool userInitiated){
  if(queue.empty()||queuePos<0)return;
  if(!userInitiated && settings.data.repeat==RepeatMode::One){startFile(queue[queuePos]);return;}
  if(settings.data.shuffle && queue.size()>1){
    int next=queuePos;while(next==queuePos)next=random(queue.size());queuePos=next;
  }else{
    queuePos++;
    if(queuePos>=(int)queue.size()){
      if(settings.data.repeat==RepeatMode::All)queuePos=0;
      else{releaseDecoder();activeTrack=-1;return;}
    }
  }
  startFile(queue[queuePos]);
}
void AudioEngine::previous(){
  if(!active())return;
  // No byte-accurate seek in this audio pipeline. Replay if >3s, otherwise previous.
  if(elapsedSeconds()>3){startFile(queue[queuePos]);return;}
  if(queuePos>0)queuePos--;
  else if(settings.data.repeat==RepeatMode::All)queuePos=queue.size()-1;
  startFile(queue[queuePos]);
}
void AudioEngine::loop(){
  if(!decoder || pausedState){
    if(clickMix)clickMix->serviceIdle();
    return;
  }
  if(decoder->isRunning()){
    if(!decoder->loop()){
      releaseDecoder();next(false);
    }
  }else {releaseDecoder();next(false);}
}
 #if FEATURE_BLUETOOTH
bool AudioEngine::switchBluetooth(bool on){
  if(on==bluetoothOn)return true;
  if(on && !btOutput.begin()){error="Bluetooth stack failed to start";return false;}

  int previous=activeTrack;
  std::vector<size_t> previousQueue=queue;
  releaseDecoder();activeTrack=-1;
  if(on){
    if(clickMix)clickMix->setTarget(&btOutput);
    sink=clickMix;
    btOutput.SetGain((settings.data.volume/100.0f)*(settings.data.volume/100.0f));
    bluetoothOn=true;
  } else {
    if(clickMix)clickMix->setTarget(i2s);
    sink=clickMix;bluetoothOn=false;
  }
  if(previous>=0)play(previous,previousQueue);
  return true;
}
#endif // FEATURE_BLUETOOTH
