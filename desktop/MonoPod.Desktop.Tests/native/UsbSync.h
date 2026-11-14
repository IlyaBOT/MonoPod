#pragma once
#include "Config.h"
#if FEATURE_USB_SYNC
#include <SD.h>
class UsbSyncService {
public:void begin();void loop();bool busy()const{return sending;}
private:
 String line;uint32_t size=0,received=0,lastByteAt=0,quietUntil=0;uint16_t chunkRemaining=0,buffered=0;
 uint8_t buffer[1024];bool sending=false,discard=false;void command(const String& cmd);void reply(const String& payload);void fail(const char* reason);void finish();
};
#else
class UsbSyncService {public:void begin(){}void loop(){}bool busy()const{return false;}};
#endif
extern UsbSyncService usbSync;
