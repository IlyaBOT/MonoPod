#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include "../DIY_iPod_Mono/ChunkedFlacSource.h"
class TestSrc : public AudioFileSource {
 public:
  uint32_t size=5*1024*1024,pos=0,readBytes=0;
  uint32_t read(void* dst,uint32_t len)override{
    len=std::min(len,size-pos);
    auto* b=static_cast<uint8_t*>(dst);
    for(uint32_t i=0;i<len;i++)b[i]=(uint8_t)(((pos+i)*13u+17u)&255u);
    pos+=len;readBytes+=len;return len;
  }
  bool seek(int32_t n,int dir)override{
    const int64_t where=(dir==SEEK_SET?0LL:(dir==SEEK_CUR?(int64_t)pos:(int64_t)size))+(int64_t)n;
    if(where<0 || where>size)return false;pos=(uint32_t)where;return true;
  }
  uint32_t getSize()override{return size;}
  uint32_t getPos()override{return pos;}
};
int main(){
  TestSrc sd;
  ChunkedFlacSource ring(&sd);
  assert(ring.capacityBytes()==2*1024*1024);
  uint8_t b[15000];
  assert(ring.read(b,1024)==1024);
  assert(sd.readBytes<16000); // playback starts before buffering ~10 seconds
  assert(ring.getPos()==1024);
  for(uint32_t i=0;i<1024;i++)assert(b[i]==(uint8_t)((i*13u+17u)&255u));
  for(int n=0;n<530;n++)ring.loop();
  assert(ring.bufferedBytes()>1900*1024);
  assert(ring.seek(200000,SEEK_SET));
  assert(ring.getPos()==200000 && ring.bufferedBytes()==0);
  assert(ring.read(b,sizeof(b))==sizeof(b));
  for(uint32_t i=0;i<sizeof(b);i++)assert(b[i]==(uint8_t)(((200000u+i)*13u+17u)&255u));
  assert(ring.seek(-2000,SEEK_CUR));
  assert(ring.getPos()==213000);
  assert(ring.read(b,3000)==3000);
  for(uint32_t i=0;i<3000;i++)assert(b[i]==(uint8_t)(((213000u+i)*13u+17u)&255u));
  assert(!ring.seek(-1,SEEK_SET));
  assert(ring.seek(-10,SEEK_END));
  assert(ring.read(b,20)==10);
  assert(ring.getPos()==sd.size);
  assert(ring.seek(0,SEEK_SET));
  for(uint32_t begin=0;begin<sd.size;begin+=sizeof(b)){
    const uint32_t n=std::min((uint32_t)sizeof(b),sd.size-begin);
    assert(ring.read(b,n)==n);
    for(uint32_t i=0;i<n;i++)assert(b[i]==(uint8_t)(((begin+i)*13u+17u)&255u));
    ring.loop();
  }
  mock_psram_free()=3u*1024u*1024u;
  { TestSrc test; ChunkedFlacSource small(&test); assert(small.capacityBytes()==1024*1024); }
  mock_psram_free()=1024u*1024u;
  { TestSrc test; ChunkedFlacSource small(&test); assert(small.capacityBytes()==512*1024); }
  mock_psram_free()=8u*1024u*1024u;
  puts("FLAC ring: 2MiB N16R8 / adaptive fallback / bounded first-fill / read-ahead / seek / EOF OK");
}
