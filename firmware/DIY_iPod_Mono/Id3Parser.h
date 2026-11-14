#pragma once
// Small, streaming ID3v1 / ID3v2.2-2.4 metadata reader. No Arduino dependencies.
// Only relevant text frames are loaded; art and private frames are skipped by seek().
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <new>

namespace Id3Parser {
constexpr size_t kValueSize=192;       // UTF-8 bytes, including terminator
// We skip artwork/private frames with seek(), so a large APIC must NOT
// prevent reading TIT2/TPE1/TALB frames located later in the tag.
constexpr uint32_t kMaxTagScan=32UL*1024UL*1024UL;
constexpr uint32_t kMaxFrameHeaders=8192; // avoid unbounded I/O on malformed tags
constexpr uint32_t kMaxTextFrame=4096; // heap, never allocated on the task stack
struct Tags {
  char title[kValueSize]={},artist[kValueSize]={},album[kValueSize]={},genre[kValueSize]={};
  uint32_t durationSeconds=0;
  uint16_t year=0;
};
inline uint32_t be32(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
inline uint32_t syncsafe(const uint8_t *p){return ((uint32_t)(p[0]&127)<<21)|((uint32_t)(p[1]&127)<<14)|((uint32_t)(p[2]&127)<<7)|(p[3]&127);}
inline bool validSyncsafe(const uint8_t *p){return !(p[0]&128 || p[1]&128 || p[2]&128 || p[3]&128);}
inline bool frameID(const uint8_t* s,size_t n){for(size_t i=0;i<n;i++)if(!((s[i]>='A'&&s[i]<='Z')||(s[i]>='0'&&s[i]<='9')))return false;return true;}
inline void emit(char* out,size_t cap,size_t& used,uint32_t cp){
  if(!cp)return;
  if(cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))cp='?';
  uint8_t b[4];size_t n=0;
  if(cp<0x80){b[n++]=cp;}
  else if(cp<0x800){b[n++]=0xc0|(cp>>6);b[n++]=0x80|(cp&63);}
  else if(cp<0x10000){b[n++]=0xe0|(cp>>12);b[n++]=0x80|((cp>>6)&63);b[n++]=0x80|(cp&63);}
  else{b[n++]=0xf0|(cp>>18);b[n++]=0x80|((cp>>12)&63);b[n++]=0x80|((cp>>6)&63);b[n++]=0x80|(cp&63);}
  if(used+n>=cap)return;
  for(size_t i=0;i<n;i++)out[used++]=(char)b[i];
  out[used]=0;
}
inline bool continuation(uint8_t b){return (b&0xc0)==0x80;}
inline void trim(char* s){
  size_t len=strlen(s);while(len && (s[len-1]==' '||s[len-1]=='\t'||s[len-1]=='\r'||s[len-1]=='\n'))s[--len]=0;
  size_t lead=0;while(s[lead]==' '||s[lead]=='\t'||s[lead]=='\r'||s[lead]=='\n')lead++;
  if(lead)memmove(s,s+lead,len-lead+1);
}
// Legacy noncompliant ID3v2 encoding=0 is sometimes actually Windows-1251.
// Require several CP1251 high letters before applying this heuristic.
inline bool likelyCp1251(const uint8_t* p,size_t n){
  size_t hi=0,letters=0;
  for(size_t i=0;i<n&&p[i];i++){if(p[i]>=128)hi++;if(p[i]>=0xc0)letters++;}
  return letters>=4 && letters*4>=hi*3;
}
inline void decode(const uint8_t* raw,size_t size,char* out,size_t cap){
  if(!cap)return;
  out[0]=0;
  if(size<2)return;
  uint8_t enc=raw[0];size_t i=1,used=0;
  if(enc==0 || enc==3){
    bool cp1251=enc==0 && likelyCp1251(raw+1,size-1);
    if(enc==3 && i+2<size && raw[i]==0xef && raw[i+1]==0xbb && raw[i+2]==0xbf)i+=3;
    while(i<size){
      uint8_t a=raw[i++];if(!a)break;
      if(enc==0){
        uint32_t cp=cp1251 && a>=0xc0?0x410+(a-0xc0):
          cp1251 && a==0xa8?0x401:cp1251 && a==0xb8?0x451:a;
        emit(out,cap,used,cp);continue;
      }
      if(a<128){emit(out,cap,used,a);continue;}
      uint32_t cp=0;int count=0;uint32_t minimum=0;
      if((a&0xe0)==0xc0){cp=a&31;count=1;minimum=0x80;}
      else if((a&0xf0)==0xe0){cp=a&15;count=2;minimum=0x800;}
      else if((a&0xf8)==0xf0){cp=a&7;count=3;minimum=0x10000;}
      else{emit(out,cap,used,'?');continue;}
      size_t save=i;bool ok=true;
      for(int j=0;j<count;j++){
        if(i>=size || !continuation(raw[i])){ok=false;break;}
        cp=(cp<<6)|(raw[i++]&63);
      }
      if(!ok || cp<minimum || cp>0x10ffff || (cp>=0xd800&&cp<=0xdfff)){
        i=save;emit(out,cap,used,'?');continue;
      }
      emit(out,cap,used,cp);
    }
  }else if(enc==1 || enc==2){
    bool little=false;
    if(enc==1 && i+1<size){
      if(raw[i]==0xff && raw[i+1]==0xfe){little=true;i+=2;}
      else if(raw[i]==0xfe && raw[i+1]==0xff){little=false;i+=2;}
    }
    while(i+1<size){
      uint32_t cp=little?raw[i]|((uint32_t)raw[i+1]<<8):((uint32_t)raw[i]<<8)|raw[i+1];i+=2;
      if(!cp)break;
      if(cp>=0xd800 && cp<=0xdbff){
        if(i+1<size){uint32_t low=little?raw[i]|((uint32_t)raw[i+1]<<8):((uint32_t)raw[i]<<8)|raw[i+1];
          if(low>=0xdc00 && low<=0xdfff){i+=2;cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);}
          else cp='?';}
        else cp='?';
      }else if(cp>=0xdc00 && cp<=0xdfff)cp='?';
      emit(out,cap,used,cp);
    }
  }
  trim(out);
}
inline void copyIfMissing(char* dst,const char* src){if(!dst[0] && src[0]){size_t n=strnlen(src,kValueSize-1);memcpy(dst,src,n);dst[n]=0;}}
inline void latin1Plain(const uint8_t* raw,size_t n,char* out,size_t cap){
  if(!cap)return;
  out[0]=0;size_t used=0;bool cp1251=likelyCp1251(raw,n);
  for(size_t i=0;i<n&&raw[i];i++){
    uint8_t a=raw[i];emit(out,cap,used,cp1251&&a>=0xc0?0x410+(a-0xc0):a);
  }
  trim(out);
}
template<class Reader> bool parse(Reader& f,Tags& tags){
  if(f.size()<10)return false;
  uint8_t h[10]={};bool found=false;
  if(!f.seek(0)||f.read(h,10)!=10)return false;
  const bool id3=(memcmp(h,"ID3",3)==0 && (h[3]==2||h[3]==3||h[3]==4) && validSyncsafe(h+6));
  if(id3){
    const uint8_t ver=h[3];
    const uint32_t tagBytes=syncsafe(h+6),fileLength=(uint32_t)f.size();
    // Bounds-checked before adding; tag size is 28-bit syncsafe.
    if(fileLength>=10 && tagBytes<=fileLength-10 && tagBytes<=kMaxTagScan){
      const uint32_t end=10+tagBytes;
      uint32_t pos=10;
      // ID3v2.3: extended size excludes the length word. ID3v2.4: includes it.
      if((h[5]&0x40) && ver>=3){
        uint8_t ext[4];
        if(pos+4<=end && f.seek(pos) && f.read(ext,4)==4){
          uint32_t n=ver==3?be32(ext):validSyncsafe(ext)?syncsafe(ext):0;
          uint32_t skip=ver==3?n+4:n;
          if(skip>=4 && skip<=end-pos)pos+=skip;else pos=end;
        }else pos=end;
      }
      uint32_t headersSeen=0;
      while(pos<=end && end-pos>=(ver==2?6u:10u) && headersSeen++<kMaxFrameHeaders){
        uint8_t fh[10]={};uint32_t headBytes=ver==2?6:10;
        if(!f.seek(pos)||f.read(fh,headBytes)!=headBytes)break;
        if(fh[0]==0)break;
        if(!frameID(fh,ver==2?3:4))break;
        uint32_t len=ver==2?((uint32_t)fh[3]<<16)|((uint32_t)fh[4]<<8)|fh[5]:
          ver==3?be32(fh+4):(validSyncsafe(fh+4)?syncsafe(fh+4):0);
        const uint32_t content=pos+headBytes;
        if(!len || len>end-content)break;
        const char* name=nullptr;
        if(ver==2){
          if(!memcmp(fh,"TT2",3))name="TIT2";
          else if(!memcmp(fh,"TP1",3))name="TPE1";
          else if(!memcmp(fh,"TAL",3))name="TALB";
          else if(!memcmp(fh,"TCO",3))name="TCON";
          else if(!memcmp(fh,"TLE",3))name="TLEN";
          else if(!memcmp(fh,"TYE",3))name="TYER";
        }else if(!memcmp(fh,"TIT2",4))name="TIT2";
        else if(!memcmp(fh,"TPE1",4))name="TPE1";
        else if(!memcmp(fh,"TALB",4))name="TALB";
        else if(!memcmp(fh,"TCON",4))name="TCON";
        else if(!memcmp(fh,"TLEN",4))name="TLEN";
        else if(!memcmp(fh,"TDRC",4))name="TDRC";
        else if(!memcmp(fh,"TYER",4))name="TYER";
        bool unsupported=false,unsync=(h[5]&0x80)!=0;
        uint32_t payloadOffset=0;
        if(ver==3)unsupported=(fh[9]&0xe0)!=0; // compression, encryption, grouping
        if(ver==4){
          unsupported=(fh[9]&0x4c)!=0; // grouping, compression, encryption
          unsync|=(fh[9]&0x02)!=0;
          // The data-length indicator is four syncsafe bytes preceding text.
          if(fh[9]&0x01){
            uint8_t dli[4];
            if(len<5 || !f.seek(content) || f.read(dli,4)!=4 || !validSyncsafe(dli))
              unsupported=true;
            else payloadOffset=4;
          }
        }
        if(name && !unsupported && len>payloadOffset && len-payloadOffset<=kMaxTextFrame){
          const uint32_t payloadLen=len-payloadOffset;
          uint8_t* raw=new(std::nothrow) uint8_t[payloadLen];
          if(raw && f.seek(content+payloadOffset) && f.read(raw,payloadLen)==payloadLen){
            uint32_t actual=payloadLen;
            if(unsync){uint32_t wr=0;for(uint32_t i=0;i<actual;i++){
              if(i>0 && raw[i]==0 && raw[i-1]==0xff)continue;
              raw[wr++]=raw[i];
            }actual=wr;}
            char value[kValueSize];decode(raw,actual,value,sizeof(value));
            if(value[0]){
              if(!strcmp(name,"TIT2"))copyIfMissing(tags.title,value);
              else if(!strcmp(name,"TPE1"))copyIfMissing(tags.artist,value);
              else if(!strcmp(name,"TALB"))copyIfMissing(tags.album,value);
              else if(!strcmp(name,"TCON"))copyIfMissing(tags.genre,value);
              else if(!strcmp(name,"TDRC") || !strcmp(name,"TYER")){
                if(strlen(value)>=4 && value[0]>='0' && value[0]<='9' &&
                   value[1]>='0' && value[1]<='9' && value[2]>='0' && value[2]<='9' &&
                   value[3]>='0' && value[3]<='9'){
                  const unsigned y=(unsigned)(value[0]-'0')*1000+(unsigned)(value[1]-'0')*100+
                    (unsigned)(value[2]-'0')*10+(unsigned)(value[3]-'0');
                  if(y>=1800 && y<=2100)tags.year=(uint16_t)y;
                }
              }
              else if(!strcmp(name,"TLEN")){
                char* tail=nullptr;unsigned long ms=strtoul(value,&tail,10);
                if(tail!=value && (!*tail || *tail==' ') && ms>=1000 && ms<86400000UL)
                  tags.durationSeconds=(ms+500)/1000;
              }
              found=true;
            }
          }
          delete[] raw;
        }
        pos=content+len;
      }
    }
  }
  // ID3v1 fallback (or complement missing v2 fields), 128 bytes at end of MP3.
  if(f.size()>=128){
    uint8_t v1[128];
    if(f.seek(f.size()-128) && f.read(v1,sizeof(v1))==sizeof(v1) && !memcmp(v1,"TAG",3)){
      char value[kValueSize];
      latin1Plain(v1+3,30,value,sizeof(value));copyIfMissing(tags.title,value);
      latin1Plain(v1+33,30,value,sizeof(value));copyIfMissing(tags.artist,value);
      latin1Plain(v1+63,30,value,sizeof(value));copyIfMissing(tags.album,value);
      if(!tags.year && v1[93]>='0' && v1[93]<='9' && v1[94]>='0' && v1[94]<='9' &&
         v1[95]>='0' && v1[95]<='9' && v1[96]>='0' && v1[96]<='9')
        tags.year=(v1[93]-'0')*1000+(v1[94]-'0')*100+(v1[95]-'0')*10+(v1[96]-'0');
      found=true;
    }
  }
  return found;
}
} // namespace Id3Parser
