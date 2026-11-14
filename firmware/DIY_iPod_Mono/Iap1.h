#pragma once
#include "Types.h"
#include "Config.h"
// iAP1 30-pin UART framing: FF 55 LEN <LEN bytes> CHECKSUM.
// CHK = two's-complement negative sum of LEN + payload.
// Subset: simple remote commands (Mode/Lingo 2). No Advanced Remote or MFi.
class Iap1Receiver {
public:
  void begin();
  Key poll();
  static bool decodeFrame(const uint8_t* packet,size_t len,Key& event);
private:
  uint8_t data[66]={};
  uint8_t used=0,expected=0,sum=0;
  enum State : uint8_t {FF,SS,LEN,BODY,CHECK} state=FF;
  Key feed(uint8_t b);
};
extern Iap1Receiver iap1;
