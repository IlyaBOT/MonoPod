#include "BadApple.h"
#if !EXCLUDE_BAD_APPLE_EASTEREGG
#include "Display.h"
#include <string.h>
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
#if BADAPPLE_HAS_EMBEDDED
  if(BadAppleEmbeddedSize>0){embedded=BadAppleEmbedded;embeddedLength=BadAppleEmbeddedSize;}
#endif
  if(!embedded){
    data=SD.open(path,FILE_READ);
    if(!data){lastError="Missing video (flash or SD)";return false;}
  }
  char magic[4];for(int i=0;i<4;i++){int b=nextByte();if(b<0){lastError="Empty video";end();return false;}magic[i]=(char)b;}
  if(memcmp(magic,"BAV1",4)!=0){lastError="Bad video header";end();return false;}
  uint16_t w=next16(),h=next16();
  fps=(uint8_t)nextByte();totalFrames=next32();
  if(w!=160||h!=128||fps<1||fps>25||!totalFrames){lastError="Unsupported BAV1";end();return false;}
  memset(pixels,0,sizeof(pixels));frame=0;nextFrame=millis();running=true;return true;
}
bool BadApplePlayer::decodeNext(){
  if((!data && !embedded) || frame>=totalFrames)return false;
  uint16_t n=next16();if(!n || n>16384 || position()+n>length())return false;
  uint32_t i=0,used=0;
  while(used<n){
    int c=nextByte();if(c<0)return false;used++;
    uint16_t count=(c&0x7f)+1;
    if(c&0x80){i+=count;}
    else {
      if(used+count>n || i+count>sizeof(pixels))return false;
      for(uint16_t j=0;j<count;j++){int v=nextByte();if(v<0)return false;pixels[i++]^=(uint8_t)v;}
      used+=count;
    }
    if(i>sizeof(pixels))return false;
  }
  return i==sizeof(pixels);
}
void BadApplePlayer::drawFrame(){
  // 2-bit grayscale; convert each packed line, push one fast SPI frame.
  uint16_t colors[4]={0x0000,0x528a,0xad55,0xffff};
  // Use frame-level stream output from Display without allocating a 40KB frame.
  display.streamGray2Frame(pixels,colors);
}
void BadApplePlayer::loop(){
  if(!running)return;
  uint32_t now=millis();if((int32_t)(now-nextFrame)<0)return;
  if(!decodeNext()){lastError="Decode/EoF";end();return;}
  drawFrame();frame++;nextFrame+=(1000UL/fps);
  if((int32_t)(millis()-nextFrame)>200)nextFrame=millis();
  if(frame>=totalFrames)end();
}
void BadApplePlayer::end(){if(data)data.close();embedded=nullptr;embeddedLength=embeddedOffset=0;running=false;}
#endif
