#include "Theme.h"
#include <SD.h>
#include <esp_heap_caps.h>
#include <cstring>
#include <cstdlib>
ThemeManager theme;
static uint16_t rd16(const uint8_t *p){return (uint16_t)p[0]|((uint16_t)p[1]<<8);}
static uint32_t rd32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint32_t themeCRC(const uint8_t *data,size_t n){
  uint32_t c=0xffffffffUL;
  for(size_t i=0;i<n;i++){
    c^=data[i];
    for(uint8_t j=0;j<8;j++)c=(c>>1)^((c&1)?0xedb88320UL:0);
  }
  return ~c;
}
ThemeManager::~ThemeManager(){unload();}
void ThemeManager::unload(){if(bytes){free(bytes);bytes=nullptr;}length=0;bigCount=smallCount=0;}
bool ThemeManager::reload(const char* path){
  File f=SD.open(path,FILE_READ);
  if(!f)return false;
  const size_t fileSize=f.size();
  if(fileSize<28 || fileSize>MAX_BYTES+28){f.close();return false;}
  uint8_t h[28];
  if(f.read(h,sizeof(h))!=sizeof(h) || memcmp(h,"MPT1",4)!=0 || h[4]!=1 || h[5]!=1){f.close();return false;}
  const uint8_t bh=h[6],sh=h[7];
  const uint16_t bc=rd16(h+8),sc=rd16(h+10);
  if(bh<8||bh>24||sh<8||sh>24||bc>256||sc>256||bc==0||sc==0){f.close();return false;}
  const uint32_t size=rd32(h+20),checksum=rd32(h+24);
  if(size!=fileSize-28 || size>MAX_BYTES){f.close();return false;}
  uint8_t *raw=(uint8_t*)heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!raw)raw=(uint8_t*)malloc(size);
  if(!raw){f.close();return false;}
  size_t got=0;
  while(got<size){int n=f.read(raw+got,min((size_t)2048,(size_t)(size-got)));if(n<=0)break;got+=(size_t)n;}
  f.close();
  if(got!=size||themeCRC(raw,size)!=checksum){free(raw);return false;}
  Glyph *decoded=(Glyph*)calloc(512,sizeof(Glyph));
  if(!decoded){free(raw);return false;}
  Glyph* newBig=decoded;Glyph* newSmall=decoded+256;
  size_t pos=0;
  for(uint8_t table=0;table<2;table++){
    uint16_t n=table?sc:bc;const uint8_t height=table?sh:bh;
    Glyph *dest=table?newSmall:newBig;
    uint32_t prior=0;
    for(uint16_t i=0;i<n;i++){
      if(pos+7>size){free(raw);free(decoded);return false;}
      const uint32_t cp=rd32(raw+pos);
      const uint8_t width=raw[pos+4],advance=raw[pos+5],reserved=raw[pos+6];pos+=7;
      if((i&&cp<=prior)||!width||width>24||!advance||advance>32||reserved){free(raw);free(decoded);return false;}
      const size_t pixelBytes=((size_t)width*height+1)/2;
      if(pos+pixelBytes>size){free(raw);free(decoded);return false;}
      dest[i]={cp,(uint32_t)pos,width,advance};pos+=pixelBytes;prior=cp;
    }
  }
  if(pos!=size){free(raw);free(decoded);return false;}
  // Replace only after complete validation: an invalid upload cannot blank the UI.
  unload();bytes=raw;length=size;bigCount=bc;smallCount=sc;bigHeight=bh;smallHeight=sh;
  memcpy(big,newBig,bc*sizeof(Glyph));memcpy(tiny,newSmall,sc*sizeof(Glyph));free(decoded);
  for(int i=0;i<4;i++)colors[i]=rd16(h+12+i*2);
  return true;
}
bool ThemeManager::glyph(uint32_t cp,bool small,Glyph& out) const {
  if(!active())return false;
  const Glyph* list=small?tiny:big;int low=0,high=(small?smallCount:bigCount)-1;
  while(low<=high){int mid=(low+high)/2;uint32_t code=list[mid].codepoint;
    if(code==cp){out=list[mid];return true;}if(code<cp)low=mid+1;else high=mid-1;
  }
  return false;
}
uint8_t ThemeManager::alpha(const Glyph& g,bool small,uint8_t x,uint8_t y) const {
  if(!bytes||x>=g.width||y>=(small?smallHeight:bigHeight))return 0;
  const uint32_t p=(uint32_t)y*g.width+x;
  const uint8_t b=bytes[g.offset+p/2];return (p&1)?(b&15):(b>>4);
}
