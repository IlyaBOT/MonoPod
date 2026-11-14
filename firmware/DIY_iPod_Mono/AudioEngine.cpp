#include "AudioEngine.h"
#include "Config.h"
#include "Library.h"
#include "Settings.h"
#include <math.h>
#if FEATURE_FLAC
#include "SeekableFlacDecoder.h"
#endif

class AudioEngineGuard {
public:
  explicit AudioEngineGuard(const AudioEngine &engine):mutex_(engine.mutex){
    if(mutex_)xSemaphoreTakeRecursive(mutex_,portMAX_DELAY);
  }
  ~AudioEngineGuard(){if(mutex_)xSemaphoreGiveRecursive(mutex_);}
private:SemaphoreHandle_t mutex_;
};
#define AUDIO_LOCK() AudioEngineGuard audioGuard(*this)
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
bool AudioEngine::active() const{AUDIO_LOCK();return activeTrack>=0;}
bool AudioEngine::paused() const{AUDIO_LOCK();return pausedState;}
int AudioEngine::current() const{AUDIO_LOCK();return activeTrack;}
uint32_t AudioEngine::ordinal() const{AUDIO_LOCK();return queuePos>=0?(uint32_t)queuePos+1:0;}
uint32_t AudioEngine::queueSize() const{AUDIO_LOCK();return (uint32_t)queue.size();}
String AudioEngine::errorText() const{AUDIO_LOCK();return error;}
bool AudioEngine::backgroundSuspended() const{AUDIO_LOCK();return backgroundSuspended_;}
AudioOutput* AudioEngine::externalAudioSink() const{AUDIO_LOCK();return sink;}
bool AudioEngine::begin() {
  if(!mutex)mutex=xSemaphoreCreateRecursiveMutex();
  if(!mutex){error="Audio mutex allocation failed";return false;}
  AUDIO_LOCK();
  i2s=new AudioOutputI2S();
  if(!i2s){error="Cannot allocate I2S";return false;}
  i2s->SetPinout(Config::PIN_I2S_BCLK,Config::PIN_I2S_LRC,Config::PIN_I2S_DATA);
#if MONOPOD_BOARD == MONOPOD_BOARD_ESP32_S3
  // 24 x 512-byte DMA buffers: ~70ms @44.1kHz, so a TFT 20ms SPI burst
  // does not drain the entire I2S queue while SD and display share the bus.
  i2s->SetBuffers(24,512);
#endif
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
  AUDIO_LOCK();
#if FEATURE_AUDIO
  if(clickMix) clickMix->trigger(frequency,durationMs);
#else
  (void)frequency; (void)durationMs;
#endif
}
void AudioEngine::stopHeadphoneClick(){
  AUDIO_LOCK();
  if(clickMix)clickMix->cancelClick();
}
void AudioEngine::setVolume(uint8_t v){
  AUDIO_LOCK();
  float gain=(constrain((int)v,0,(int)Config::MAX_VOLUME)/100.0f);
  // 1.5-power taper: 30% remains audible (older square-law mapped it
  // to just 9% amplitude). 0 stays exactly silent; 100 stays unity gain.
  gain=gain*sqrtf(gain);
  if(i2s)i2s->SetGain(gain);
#if FEATURE_BLUETOOTH
  btOutput.SetGain(gain);
#endif
}
void AudioEngine::releaseDecoder() {
  if(decoder){decoder->stop();delete decoder;decoder=nullptr;}
#if FEATURE_FLAC
  if(flacBuffer){delete flacBuffer;flacBuffer=nullptr;}
#endif
  if(source){source->close();delete source;source=nullptr;}
}
void AudioEngine::stop() {
  AUDIO_LOCK();
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
  else if(p.endsWith(".flac")){
    flacBuffer=new ChunkedFlacSource(source);
    if(flacBuffer)MONOPOD_LOG.printf("FLAC: compressed buffer=%lu KiB, free PSRAM=%lu KiB\n",
      (unsigned long)(flacBuffer->capacityBytes()/1024),
      (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)/1024));
    // No full-file prefetch: buffer is filled on demand in small SD chunks.
    decoder=new SeekableFlacDecoder();
  }
#endif
#if FEATURE_AAC
  else if(p.endsWith(".aac"))decoder=new AudioGeneratorAAC();
#endif
  else{error="Unsupported format";releaseDecoder();activeTrack=-1;return false;}
  if(!decoder){error="Decoder allocation failed";releaseDecoder();activeTrack=-1;return false;}
  AudioFileSource *decoderSource=source;
#if FEATURE_FLAC
  if(flacBuffer)decoderSource=flacBuffer;
#endif
  if(!decoder->begin(decoderSource,sink)){error="Decoder begin failed";releaseDecoder();activeTrack=-1;return false;}
  activeTrack=(int)index;playStarted=millis();error="";return true;
}
bool AudioEngine::play(size_t libraryIndex,const std::vector<size_t>& context){
  AUDIO_LOCK();
  if(!sink){error="Audio disabled or output not initialized";return false;}
  queue=context;
  if(queue.empty())media.allIndices(queue);
  queuePos=-1;for(size_t i=0;i<queue.size();i++)if(queue[i]==libraryIndex){queuePos=(int)i;break;}
  if(queuePos<0){queue.insert(queue.begin(),libraryIndex);queuePos=0;}
  return startFile(libraryIndex);
}
void AudioEngine::pauseToggle(){
  AUDIO_LOCK();
  if(!active())return;
  if(pausedState){pausedMs+=millis()-pauseStarted;pausedState=false;}
  else{pausedState=true;pauseStarted=millis();}
}
uint32_t AudioEngine::elapsedSeconds() const {
  AUDIO_LOCK();
  if(!active())return 0;
  uint32_t end=pausedState?pauseStarted:millis();
  return seekBaseSeconds+(end-playStarted-pausedMs)/1000;
}
uint32_t AudioEngine::durationSeconds() const {
  AUDIO_LOCK();
  if(activeTrack<0 || (size_t)activeTrack>=media.size())return 0;
  return media.at((size_t)activeTrack).durationSeconds;
}
uint32_t AudioEngine::remainingSeconds() const {
  uint32_t duration=durationSeconds();
  uint32_t elapsed=elapsedSeconds();
  return duration>elapsed?duration-elapsed:0;
}
bool AudioEngine::seekSeconds(uint32_t seconds){
  AUDIO_LOCK();
  if(!active() || !source || !decoder)return false;
  const uint32_t duration=durationSeconds();
  if(duration==0)return false;
  String path=media.at((size_t)activeTrack).path;path.toLowerCase();
#if FEATURE_FLAC
  if(path.endsWith(".flac")){
    const Track &track=media.at((size_t)activeTrack);
    if(!track.flacSampleRate || !track.flacTotalSamples)return false;
    seconds=min(seconds,duration-1);
    uint64_t sample=(uint64_t)seconds*track.flacSampleRate;
    if(sample>=track.flacTotalSamples)sample=track.flacTotalSamples-1;
    SeekableFlacDecoder *flac=static_cast<SeekableFlacDecoder*>(decoder);
    const bool wasPaused=pausedState;
    if(!flac->seekToSample(sample)){
      // A failed libFLAC seek may leave its internal state in SEEK_ERROR.
      // Recreate the decoder rather than trying to continue corrupted state.
      const int index=activeTrack;
      startFile((size_t)index);
      return false;
    }
    seekBaseSeconds=seconds;playStarted=millis();pausedMs=0;
    pausedState=wasPaused;pauseStarted=playStarted;
    return true;
  }
#endif
  if(!path.endsWith(".mp3") && !path.endsWith(".wav"))return false;
  const uint32_t length=source->getSize();if(length<128)return false;
  seconds=min(seconds,duration-1); // avoid EOF and unexpected next-track
  const uint32_t position=(uint32_t)((uint64_t)length*seconds/duration);
  const int index=activeTrack;
  const bool wasPaused=pausedState;
  // Approximate offset for CBR MP3/WAV. VBR MP3 requires a frame/TOC index
  // for sample-accurate seeking. Preserve pause and never under/overflow time.
  if(!startFile((size_t)index))return false;
  if(!source->seek((int32_t)position,SEEK_SET)){
    startFile((size_t)index);return false;
  }
  seekBaseSeconds=seconds;playStarted=millis();pausedMs=0;
  pausedState=wasPaused;pauseStarted=playStarted;
  return true;
}
bool AudioEngine::seekPercent(uint8_t pct){
  AUDIO_LOCK();
  pct=(uint8_t)constrain((int)pct,0,99);
  const uint32_t duration=durationSeconds();
  if(duration==0)return false;
  return seekSeconds((uint32_t)((uint64_t)duration*pct/100u));
}
uint8_t AudioEngine::approximateProgress() const {
  AUDIO_LOCK();
  if(!source || !source->getSize())return 0;
  if(durationSeconds())return (uint8_t)min(100UL,100UL*elapsedSeconds()/durationSeconds());
  return (uint8_t)min(100UL,(100UL*source->getPos())/source->getSize());
}
void AudioEngine::next(bool userInitiated){
  AUDIO_LOCK();
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
  AUDIO_LOCK();
  if(!active())return;
  // No byte-accurate seek in this audio pipeline. Replay if >3s, otherwise previous.
  if(elapsedSeconds()>3){startFile(queue[queuePos]);return;}
  if(queuePos>0)queuePos--;
  else if(settings.data.repeat==RepeatMode::All)queuePos=queue.size()-1;
  startFile(queue[queuePos]);
}
void AudioEngine::suspendBackground(bool suspend){
  AUDIO_LOCK();backgroundSuspended_=suspend;
}
void AudioEngine::loop(){
  AUDIO_LOCK();
  if(backgroundSuspended_)return;
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
  AUDIO_LOCK();
  if(on==bluetoothOn)return true;
  if(on && !btOutput.begin()){error="Bluetooth stack failed to start";return false;}

  int previous=activeTrack;
  std::vector<size_t> previousQueue=queue;
  releaseDecoder();activeTrack=-1;
  if(on){
    if(clickMix)clickMix->setTarget(&btOutput);
    sink=clickMix;
    const float g=(settings.data.volume/100.0f);
    btOutput.SetGain(g*sqrtf(g));
    bluetoothOn=true;
  } else {
    if(clickMix)clickMix->setTarget(i2s);
    sink=clickMix;bluetoothOn=false;
  }
  if(previous>=0)play(previous,previousQueue);
  return true;
}
#endif // FEATURE_BLUETOOTH
