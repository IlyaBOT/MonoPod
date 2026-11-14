#include "BadApple.h"
#if !EXCLUDE_BAD_APPLE_EASTEREGG
#include "Display.h"
#include "AudioEngine.h"
#include <string.h>
#include <stdlib.h>
#if defined(ARDUINO_ARCH_ESP32)
#include <esp_heap_caps.h>
#endif
#if __has_include("BadAppleEmbedded.h")
#include "BadAppleEmbedded.h"
#define BADAPPLE_HAS_EMBEDDED 1
#else
#define BADAPPLE_HAS_EMBEDDED 0
#endif
BadApplePlayer badApple;
int BadApplePlayer::nextByte(){
  if(embedded)return embeddedOffset<embeddedLength?embedded[embeddedOffset++]:-1;
  return data.read();
}
uint16_t BadApplePlayer::next16(){int a=nextByte(),b=nextByte();return a<0||b<0?0:((uint16_t)a|((uint16_t)b<<8));}
uint32_t BadApplePlayer::next32(){uint32_t a=next16(),b=next16();return a|(b<<16);}
uint32_t BadApplePlayer::length(){return embedded?embeddedLength:(uint32_t)data.size();}
uint32_t BadApplePlayer::position(){return embedded?embeddedOffset:(uint32_t)data.position();}
bool BadApplePlayer::begin(const char* path){
  end();lastError="";
  data=SD.open(path,FILE_READ);
#if BADAPPLE_HAS_EMBEDDED
  if(!data && BadAppleEmbeddedSize>0){
    embedded=BadAppleEmbedded;embeddedLength=BadAppleEmbeddedSize;
  }
#endif
  if(!data && !embedded){lastError="Missing video (flash or SD)";return false;}
  if(length()<13){lastError="Short BAV header";end();return false;}
  char magic[4];for(int i=0;i<4;i++){int b=nextByte();if(b<0){lastError="Empty video";end();return false;}magic[i]=(char)b;}
  const bool v3=memcmp(magic,"BAV3",4)==0;
  const bool v2=memcmp(magic,"BAV2",4)==0;
  if(!v3 && !v2 && memcmp(magic,"BAV1",4)!=0){lastError="Bad BAV header";end();return false;}
  const uint16_t w=next16(),h=next16();
  fps=v3?next16():(uint16_t)(uint8_t)nextByte();
  totalFrames=next32();
  uint32_t audioOffset=0,audioSize=0;
  uint8_t audioCodec=0;
  if(v3 || v2){audioOffset=next32();audioSize=next32();audioCodec=(uint8_t)nextByte();}
  pixelFormat=GRAY2;
  if(v3){pixelFormat=(PixelFormat)(uint8_t)nextByte();(void)nextByte();/* reserved flags */}
  // BAV3 header: 25 bytes; BAV2 header: 22 bytes; BAV1 header: 13 bytes.
  const uint32_t headerEnd=position();
  if(w!=160 || h!=128 || fps<1 || fps>120 || !totalFrames ||
      (v3 && pixelFormat!=GRAY6 && pixelFormat!=RGB565) ||
      ((v2||v3) && ((audioCodec!=0 && audioCodec!=1) ||
          (audioCodec==1 && audioSize==0) || (audioCodec==0 && audioSize!=0) ||
          audioOffset<headerEnd ||
          (uint64_t)audioOffset+audioSize>length()))){
    lastError="Unsupported BAV";end();return false;
  }
  frameBytes=(pixelFormat==RGB565)?40960u:(pixelFormat==GRAY6)?15360u:5120u;
#if defined(ARDUINO_ARCH_ESP32)
  if(psramFound())pixels=(uint8_t*)heap_caps_malloc(frameBytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#endif
  if(!pixels)pixels=(uint8_t*)malloc(frameBytes);
  if(!pixels){lastError="BAV frame RAM allocation failed";end();return false;}
  memset(pixels,0,frameBytes);
  videoEnd=(v2||v3)?audioOffset:length();
  if(videoEnd<=headerEnd){lastError="BAV video section empty";end();return false;}
  if((v2||v3) && audioCodec==1){
    if(embedded){lastError="Embedded BAV audio unsupported";end();return false;}
    if(!startAudio(path,audioOffset,audioSize)){
      lastError="MP3 audio init failed";end();return false;
    }
  }
  frame=0;startMs=millis();fpsLogAt=startMs;fpsLogFrames=0;totalDrawUs=0;running=true;return true;
}
bool BadApplePlayer::startAudio(const char* path,uint32_t offset,uint32_t size){
  if(!player.externalAudioSink())return false;
  audioSource=new BadAppleAudioSource(path,offset,size);
  if(!audioSource || !audioSource->isOpen())return false;
  audioDecoder=new AudioGeneratorMP3();
  return audioDecoder && audioDecoder->begin(audioSource,player.externalAudioSink());
}
bool BadApplePlayer::decodeNext(){
  if((!data && !embedded) || frame>=totalFrames || !pixels)return false;
  if(position()+2u>videoEnd)return false;
  const uint16_t n=next16();
  // Packet always contains full frame's delta; up to 40960 raw bytes + RLE overhead.
  if(!n || (uint64_t)position()+n>videoEnd)return false;
  size_t i=0;uint32_t used=0;
  while(used<n){
    const int c=nextByte();if(c<0)return false;used++;
    const size_t count=(size_t)(c&0x7f)+1u;
    if(c&0x80){
      if(count>frameBytes-i)return false;
      i+=count;
    }else{
      if(used+count>n || count>frameBytes-i)return false;
      for(size_t j=0;j<count;j++){
        const int v=nextByte();if(v<0)return false;
        pixels[i++]^=(uint8_t)v;
      }
      used+=(uint32_t)count;
    }
  }
  return i==frameBytes;
}
void BadApplePlayer::drawFrame(){
  if(pixelFormat==RGB565){display.streamRgb565Frame(pixels);return;}
  if(pixelFormat==GRAY6){display.streamGray6Frame(pixels);return;}
  const uint16_t colors[4]={0x0000,0x528a,0xad55,0xffff};
  display.streamGray2Frame(pixels,colors);
}
void BadApplePlayer::loop(){
  if(!running)return;
  if(audioDecoder && audioDecoder->isRunning())audioDecoder->loop();
  const uint32_t deadline=startMs+(uint32_t)(((uint64_t)frame*1000u)/fps);
  if((int32_t)(millis()-deadline)<0)return;
  if(!decodeNext()){
    lastError="Video frame decoding failed";end();return;
  }
  const uint32_t drawStart=micros();
  drawFrame();
  totalDrawUs+=(uint32_t)(micros()-drawStart);
  fpsLogFrames++;
  frame++;
  const uint32_t elapsed=millis()-fpsLogAt;
  if(elapsed>=5000 && fpsLogFrames){
    MONOPOD_LOG.printf("[BAV] fps=%lu.%01lu draw_avg=%lu us frame=%lu/%lu target=%u\n",
      (unsigned long)((uint64_t)fpsLogFrames*1000u/elapsed),
      (unsigned long)(((uint64_t)fpsLogFrames*10000u/elapsed)%10u),
      (unsigned long)(totalDrawUs/fpsLogFrames),
      (unsigned long)frame,(unsigned long)totalFrames,(unsigned)fps);
    fpsLogFrames=0;totalDrawUs=0;fpsLogAt=millis();
  }
  if(frame>=totalFrames)end();
}
void BadApplePlayer::end(){
  if(audioDecoder){audioDecoder->stop();delete audioDecoder;audioDecoder=nullptr;}
  if(audioSource){audioSource->close();delete audioSource;audioSource=nullptr;}
  if(data)data.close();embedded=nullptr;embeddedLength=embeddedOffset=0;
  if(pixels){free(pixels);pixels=nullptr;}
  frameBytes=0;videoEnd=0;running=false;
}
#endif
