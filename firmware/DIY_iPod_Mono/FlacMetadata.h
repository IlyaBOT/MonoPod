#pragma once
// FLAC STREAMINFO and VORBIS_COMMENT parser. Bounded reads; no full-file buffering.
// Portable: any seekable File-like object supporting read(), seek(), size().
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace FlacMetadata {
struct Info {
  char title[81]={},artist[81]={},album[81]={},albumArtist[81]={},genre[81]={};
  uint16_t trackNumber=0;
  uint16_t year=0;
  uint32_t sampleRate=0;
  uint64_t totalSamples=0;
  uint32_t durationSeconds=0;
};
static inline uint32_t be24(const uint8_t *p){return (uint32_t(p[0])<<16)|(uint32_t(p[1])<<8)|p[2];}
static inline uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static inline bool eqAscii(const uint8_t *a,size_t n,const char *b){
  size_t i=0;for(;i<n && b[i];++i){char c=(char)a[i];if(c>='a'&&c<='z')c-=32;if(c!=b[i])return false;}
  return i==n && b[i]==0;
}
static inline void boundedCopy(char *dst,size_t cap,const uint8_t *src,size_t n){
  if(!cap)return;
  n=n<cap-1?n:cap-1;
  // Strip embedded control bytes, preserving UTF-8 metadata.
  size_t j=0;for(size_t i=0;i<n;i++)if(src[i]>=32)dst[j++]=(char)src[i];dst[j]=0;
}
template<typename FileT> static bool readExact(FileT& f,uint8_t *p,size_t n){
  return n==0 || f.read(p,n)==(int)n;
}
template<typename FileT> static bool skip(FileT& f,uint32_t bytes){
  uint64_t end=(uint64_t)f.position()+bytes;
  return end<=f.size() && f.seek((uint32_t)end);
}
template<typename FileT> static bool comments(FileT& f,uint32_t length,Info& info){
  // Do not allocate a full VORBIS_COMMENT block. Never parse outside the block.
  uint32_t left=length;uint8_t u[4];
  if(left<8 || !readExact(f,u,4))return false;
  uint32_t vendor=le32(u);left-=4;
  if(vendor>left-4 || !skip(f,vendor))return false;
  left-=vendor;
  if(!readExact(f,u,4))return false;
  uint32_t count=le32(u);left-=4;
  if(count>4096)return false;
  for(uint32_t i=0;i<count;i++){
    if(left<4 || !readExact(f,u,4))return false;
    uint32_t size=le32(u);left-=4;
    if(size>left)return false;
    // Large embedded fields aren't tags; avoid heap allocation and skip safely.
    if(size>4096){if(!skip(f,size))return false;left-=size;continue;}
    // Keep this stack frame small: scanFolder recurses on the Arduino loopTask.
    uint8_t field[256];
    const uint32_t captured=size<sizeof(field)?size:sizeof(field);
    if(!readExact(f,field,captured))return false;
    if(!skip(f,size-captured))return false;
    left-=size;
    size_t eq=0;while(eq<captured && field[eq]!='=')eq++;
    if(eq==captured)continue;
    const uint8_t *value=field+eq+1;size_t n=captured-eq-1;
    if(eqAscii(field,eq,"TITLE"))boundedCopy(info.title,sizeof(info.title),value,n);
    else if(eqAscii(field,eq,"ARTIST"))boundedCopy(info.artist,sizeof(info.artist),value,n);
    else if(eqAscii(field,eq,"ALBUM"))boundedCopy(info.album,sizeof(info.album),value,n);
    else if(eqAscii(field,eq,"ALBUMARTIST")||eqAscii(field,eq,"ALBUM ARTIST"))boundedCopy(info.albumArtist,sizeof(info.albumArtist),value,n);
    else if(eqAscii(field,eq,"TRACKNUMBER")){uint16_t no=0;for(size_t j=0;j<n;j++){if(value[j]<'0'||value[j]>'9')break;no=no*10+(value[j]-'0');if(no>999)break;}if(no>0&&no<=999)info.trackNumber=no;}
    else if(eqAscii(field,eq,"GENRE"))boundedCopy(info.genre,sizeof(info.genre),value,n);
    else if(eqAscii(field,eq,"DATE")||eqAscii(field,eq,"YEAR")){
      if(n>=4 && value[0]>='0'&&value[0]<='9'&&value[1]>='0'&&value[1]<='9'&&value[2]>='0'&&value[2]<='9'&&value[3]>='0'&&value[3]<='9')
        info.year=(uint16_t)((value[0]-'0')*1000+(value[1]-'0')*100+(value[2]-'0')*10+(value[3]-'0'));
    }
  }
  return skip(f,left);
}
template<typename FileT> static bool parse(FileT& f,Info& info){
  if(!f || f.size()<42 || !f.seek(0))return false;
  uint8_t head[4];if(!readExact(f,head,4)||memcmp(head,"fLaC",4))return false;
  bool gotInfo=false;
  for(uint32_t blocks=0;blocks<64;blocks++){
    if(!readExact(f,head,4))return false;
    const bool last=(head[0]&0x80)!=0;
    const uint8_t type=head[0]&0x7f;
    const uint32_t len=be24(head+1);
    const uint64_t end=(uint64_t)f.position()+len;
    if(end>f.size())return false;
    if(type==0){
      if(len!=34 || gotInfo)return false;
      uint8_t b[34];if(!readExact(f,b,34))return false;
      const uint64_t packed=(uint64_t(b[10])<<56)|(uint64_t(b[11])<<48)|
        (uint64_t(b[12])<<40)|(uint64_t(b[13])<<32)|(uint64_t(b[14])<<24)|
        (uint64_t(b[15])<<16)|(uint64_t(b[16])<<8)|b[17];
      info.sampleRate=(uint32_t)(packed>>44)&0xfffff;
      info.totalSamples=packed & ((uint64_t(1)<<36)-1);
      if(info.sampleRate && info.totalSamples)
        info.durationSeconds=(uint32_t)((info.totalSamples+info.sampleRate/2)/info.sampleRate);
      gotInfo=true;
    }else if(type==4){
      // A malformed optional comment block must not invalidate valid STREAMINFO.
      if(!comments(f,len,info))return false;
    }else if(!skip(f,len))return false;
    if((uint64_t)f.position()!=end)return false;
    if(last)return gotInfo;
  }
  return false;
}
}
