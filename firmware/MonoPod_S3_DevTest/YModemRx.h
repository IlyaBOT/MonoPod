#pragma once
// Standalone YMODEM-CRC receiver. No Arduino dependencies: native unit-testable.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

namespace YModem {
constexpr uint8_t SOH=0x01, STX=0x02, EOT=0x04, ACK=0x06, NAK=0x15, CAN=0x18, CRC_REQ='C';
inline uint16_t crc16(const uint8_t* b,size_t n) {
  uint16_t crc=0;
  for(size_t i=0;i<n;i++) { crc^=(uint16_t)b[i]<<8;
    for(int k=0;k<8;k++) crc= (crc&0x8000) ? (uint16_t)((crc<<1)^0x1021) : (uint16_t)(crc<<1);
  }
  return crc;
}
struct Stats {
  uint32_t packetsOk=0, crcErrors=0, frameErrors=0, seqErrors=0, timeouts=0;
};
struct Callbacks {
  void* context;
  bool (*begin)(void*,const char*,uint32_t);
  bool (*write)(void*,const uint8_t*,size_t);
  void (*done)(void*);
  void (*abort)(void*);
};
struct Action {
  uint8_t response[2]={0,0};
  uint8_t length=0;
  bool progress=false, finished=false, error=false;
  static Action single(uint8_t a) { Action x; x.response[0]=a; x.length=1; return x; }
  static Action two(uint8_t a,uint8_t b) { Action x; x.response[0]=a;x.response[1]=b;x.length=2;return x; }
};
enum class State : uint8_t { Idle, Header, Data, SecondEot, LastHeader, Complete, Failed };
class Receiver {
public:
  explicit Receiver(Callbacks cb):cb_(cb){}
  void start(uint32_t now) { stats_={}; state_=State::Header; packetTarget_=packetPos_=0; size_=received_=0;
    expected_=1; faults_=0; lastActivity_=now; lastRequest_=now; }
  State state() const {return state_;}
  Stats stats() const {return stats_;}
  bool active()const{return state_==State::Header||state_==State::Data||state_==State::SecondEot||state_==State::LastHeader;}
  uint32_t bytes() const{return received_;}
  uint32_t fileSize() const{return size_;}
  Action greeting()const {return Action::single(CRC_REQ);}
  Action tick(uint32_t now) {
    if(!active())return {};
    if(packetTarget_) {
      if((uint32_t)(now-lastActivity_)>2500) {packetTarget_=packetPos_=0; lastActivity_=now;stats_.timeouts++;return reject();}
      return {};
    }
    if((uint32_t)(now-lastActivity_)>16000) return fail();
    if((state_==State::Header||state_==State::LastHeader) && (uint32_t)(now-lastRequest_)>1200) {
      lastRequest_=now; return Action::single(CRC_REQ);
    }
    return {};
  }
  Action input(uint8_t b,uint32_t now) {
    if(!active())return {};
    lastActivity_=now;
    if(packetTarget_) {
      packet_[packetPos_++]=b;
      if(packetPos_==packetTarget_) {packetTarget_=packetPos_=0;return frame();}
      return {};
    }
    if(b==SOH||b==STX){ payloadLen_=b==SOH?128:1024;packetTarget_=payloadLen_+4;packetPos_=0;return {}; }
    if(b==CAN) return fail();
    if(b==EOT){
      if(state_==State::Data){state_=State::SecondEot;return Action::single(NAK);}
      if(state_==State::SecondEot){state_=State::LastHeader;lastRequest_=now;return Action::two(ACK,CRC_REQ);}
    }
    return {};
  }
  void reset(){state_=State::Idle;packetPos_=packetTarget_=0;}
private:
  Action reject() {
    if(++faults_>12) return fail();
    return Action::single(NAK);
  }
  Action fail(){
    if(cb_.abort)cb_.abort(cb_.context);
    state_=State::Failed;packetPos_=packetTarget_=0;
    Action x=Action::two(CAN,CAN);x.error=true;return x;
  }
  Action frame(){
    uint8_t seq=packet_[0];
    if((uint8_t)(seq^packet_[1])!=0xff){stats_.frameErrors++;return reject();}
    uint16_t actual=crc16(packet_+2,payloadLen_);
    uint16_t incoming=(uint16_t)packet_[payloadLen_+2]<<8 | packet_[payloadLen_+3];
    if(actual!=incoming){stats_.crcErrors++;return reject();}
    faults_=0;
    if(state_==State::Header){
      if(seq!=0 || payloadLen_!=128){stats_.seqErrors++;return reject();}
      const uint8_t* p=packet_+2;
      size_t end=0;
      while(end<128&&p[end]&&end<64)end++;
      if(end==0 || end>=64 || end+1>=128)return fail();
      char filename[65];memcpy(filename,p,end);filename[end]=0;
      const char* digits=(const char*)p+end+1;
      if(digits>=(const char*)p+128 || !isdigit((unsigned char)*digits))return fail();
      uint64_t n=0;
      for(size_t i=0;i<12 && digits[i]>='0'&&digits[i]<='9';i++) {n=n*10+(digits[i]-'0');if(n>0xffffffffULL)return fail();}
      if(n==0 || !cb_.begin || !cb_.begin(cb_.context,filename,(uint32_t)n))return fail();
      size_=(uint32_t)n;received_=0;expected_=1;state_=State::Data;
      stats_.packetsOk++;return Action::two(ACK,CRC_REQ);
    }
    if(state_==State::Data || state_==State::SecondEot) {
      if(seq==(uint8_t)(expected_-1))return Action::single(ACK); // retransmitted previous data frame
      if(state_!=State::Data || seq!=expected_){stats_.seqErrors++;return reject();}
      size_t n=(size_ - received_);if(n>payloadLen_)n=payloadLen_;
      if(n && (!cb_.write || !cb_.write(cb_.context,packet_+2,n)))return fail();
      received_+=(uint32_t)n;expected_++;
      stats_.packetsOk++;Action a=Action::single(ACK);a.progress=true;return a;
    }
    if(state_==State::LastHeader){
      if(seq!=0 || payloadLen_!=128 || packet_[2]!=0 || received_!=size_)return fail();
      state_=State::Complete;stats_.packetsOk++;
      Action x=Action::single(ACK);x.finished=true;
      if(cb_.done)cb_.done(cb_.context);
      return x;
    }
    return reject();
  }
  Callbacks cb_;
  Stats stats_{};
  State state_=State::Idle;
  uint8_t packet_[1028]={0};
  size_t payloadLen_=0,packetTarget_=0,packetPos_=0;
  uint32_t lastActivity_=0,lastRequest_=0,size_=0,received_=0;
  uint8_t expected_=1,faults_=0;
};
}
