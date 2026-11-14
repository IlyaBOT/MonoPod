#pragma once
#include "Config.h"
#if FEATURE_USB_SYNC
#include <SD.h>
class UsbSyncService {
public:
  void begin();
  void loop();
  bool busy() const {return sending;}
private:
  File upload;
  String line, fileName;
  uint32_t size=0, received=0, receivedCrc=0xFFFFFFFFUL, expectedCrc=0;
  uint16_t chunkRemaining=0;
  uint8_t buffer[512];
  uint16_t buffered=0;
  uint32_t lastByteAt=0;
  bool sending=false, replacing=false;
  void command(const String& command);
  void fail(const char* reason);
  void reset(bool removePart);
  bool flushBuffer();
  void finish();
  void reply(const String& payload);
};
#else
class UsbSyncService {public: void begin(){} void loop(){} bool busy()const{return false;} };
#endif
extern UsbSyncService usbSync;
