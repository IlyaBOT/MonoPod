#pragma once
// Incremental compressed FLAC ring buffer in PSRAM. It never preloads an entire
// song or blocks the first decoder read on a large PSRAM prefill.
#include <AudioFileSource.h>
#include <esp_heap_caps.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
class ChunkedFlacSource final : public AudioFileSource {
public:
  // 2 MiB of compressed FLAC read-ahead on N16R8 (~17-24s at 0.7-1.0 Mbps).
  // The file is NOT preloaded: playback starts with a small 12 KiB read.
  static constexpr uint32_t WANT_BYTES=2*1024*1024;
  static constexpr uint32_t INITIAL_BYTES=12*1024;
  static constexpr uint32_t REFILL_BYTES=4096;
  explicit ChunkedFlacSource(AudioFileSource *wrapped):src(wrapped){
    if(psramFound()){
      // Reserve at least half of currently available PSRAM for artwork,
      // framebuffer, track metadata and Wi-Fi/OTA subsystems. On fragmented
      // heaps, heap_caps_malloc can still fail; try progressively smaller.
      const size_t psramFree=heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      for(uint32_t wanted=WANT_BYTES;wanted>=128*1024;wanted/=2){
        if((size_t)wanted>psramFree/2)continue;
        ring=(uint8_t*)heap_caps_malloc(wanted,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(ring){capacity=wanted;break;}
      }
    }
    if(!ring){ring=(uint8_t*)malloc(32*1024);if(ring)capacity=32*1024;}
  }
  ~ChunkedFlacSource() override {free(ring);}
  bool isOpen() override{return src && src->isOpen();}
  bool close() override {used=0;return src && src->close();}
  uint32_t getSize() override{return src?src->getSize():0;}
  uint32_t getPos() override{return logicalPos;}
  bool seek(int32_t pos,int dir) override {
    if(!src)return false;
    const int64_t base=dir==SEEK_CUR?(int64_t)logicalPos:dir==SEEK_END?(int64_t)getSize():0;
    if(dir!=SEEK_CUR && dir!=SEEK_END && dir!=SEEK_SET)return false;
    const int64_t target=base+pos;
    if(target<0||target>getSize()||target>INT32_MAX)return false;
    // No refill or stale compressed data after a FLAC lib seek.
    if(!src->seek((int32_t)target,SEEK_SET))return false;
    logicalPos=(uint32_t)target;head=tail=used=0;return true;
  }
  uint32_t read(void *dst,uint32_t wanted) override {
    if(!src || !wanted)return 0;
    if(!ring){uint32_t n=src->read(dst,wanted);logicalPos+=n;return n;}
    uint8_t* p=(uint8_t*)dst;uint32_t count=0;
    while(wanted){
      if(!used){
        // Small initial read on demand, NOT an entire track or 2 MiB.
        fill(std::min(INITIAL_BYTES,capacity-1));
        if(!used)break;
      }
      uint32_t n=std::min(wanted,std::min(used,capacity-head));
      memcpy(p+count,ring+head,n);head=(head+n)%capacity;used-=n;
      logicalPos+=n;wanted-=n;count+=n;
    }
    return count;
  }
  uint32_t readNonBlock(void *dst,uint32_t len) override{return read(dst,len);}
  bool loop() override{
    if(src)src->loop();
    // At most 4 KiB of SPI work per decoder loop. No big, blocking prefill.
    if(ring && used<capacity-1)fill(REFILL_BYTES);
    return true;
  }
  uint32_t bufferedBytes()const{return used;}
  uint32_t capacityBytes()const{return capacity;}
private:
  void fill(uint32_t budget){
    if(!ring || !src)return;
    while(budget && used<capacity-1){
      uint32_t n=std::min(budget,std::min(capacity-used-1,capacity-tail));
      if(!n)break;
      uint32_t got=src->read(ring+tail,n);
      tail=(tail+got)%capacity;used+=got;budget-=got;
      if(got<n)break;
    }
  }
  AudioFileSource* src=nullptr;uint8_t *ring=nullptr;
  uint32_t capacity=0,head=0,tail=0,used=0,logicalPos=0;
};
