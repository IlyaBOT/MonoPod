#include "Iap1.h"
#include <HardwareSerial.h>
Iap1Receiver iap1;
void Iap1Receiver::begin(){
#if FEATURE_IAP1_RX
  // Passive sniff/control. No TX, handshake, 30-pin dock or Apple USB identification.
  Serial2.begin(19200,SERIAL_8N1,Config::PIN_IAP1_RX,-1);
#endif
}
Key Iap1Receiver::poll(){
#if FEATURE_IAP1_RX
  for(int bytes=0;bytes<32 && Serial2.available();bytes++){
    Key k=feed(Serial2.read());if(k!=Key::None)return k;
  }
#endif
  return Key::None;
}
Key Iap1Receiver::feed(uint8_t b){
  switch(state){
  case FF:if(b==0xff)state=SS;break;
  case SS:state=b==0x55?LEN:b==0xff?SS:FF;break;
  case LEN:
    if(b<2||b>64){state=FF;break;}
    expected=b;used=0;sum=b;state=BODY;break;
  case BODY:
    data[used++]=b;sum+=b;
    if(used>=expected)state=CHECK;
    break;
  case CHECK:{
    state=FF;
    if((uint8_t)(sum+b)!=0)return Key::None;
    Key evt=Key::None;
    if(data[0]==0x02 && data[1]==0x00 && expected>=3){
      uint8_t cmd=data[2];
      if(cmd==0x01)evt=Key::PlayPause;
      if(cmd==0x08)evt=Key::Next;
      if(cmd==0x10)evt=Key::Previous;
      if(cmd==0x02)evt=Key::Up;
      if(cmd==0x04)evt=Key::Down;
      if(expected>=5 && data[2]==0 && data[3]==0){
        if(data[4]==0x40)evt=Key::Menu;
        if(data[4]==0x80)evt=Key::Select;
      }
      if(expected>=6 && data[2]==0 && data[3]==0 && data[4]==0){
        if(data[5]==0x01)evt=Key::Up;
        if(data[5]==0x02)evt=Key::Down;
      }
    }
    return evt;
  }
  }
  return Key::None;
}
bool Iap1Receiver::decodeFrame(const uint8_t*p,size_t len,Key&out){
  Iap1Receiver r;out=Key::None;
  for(size_t i=0;i<len;i++){Key k=r.feed(p[i]);if(k!=Key::None)out=k;}
  return out!=Key::None;
}
