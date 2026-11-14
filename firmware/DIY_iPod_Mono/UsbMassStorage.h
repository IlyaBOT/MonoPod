#pragma once
#include "Config.h"
#if MONOPOD_EXPERIMENTAL_USB_MSC
#include "Types.h"
#include <USB.h>
#include <USBMSC.h>
#include <SdFat.h>
class UsbMassStorageService {
 public:
  void begin();void loop();bool busy()const{return hostHasMedia;}
 private:
  USBMSC msc;
  SdCardFactory rawFactory;
  SdCard* rawCard=nullptr;
  uint32_t sectors=0,advertisedSectorCount=0;
  bool hostHasMedia=false,ejectRequested=false,ejectedUntilUnplug=false,noSdWarned=false;
  bool lastConfigured=false;
  static UsbMassStorageService* instance;
  static int32_t onRead(uint32_t lba,uint32_t offset,void* buffer,uint32_t size);
  static int32_t onWrite(uint32_t lba,uint32_t offset,uint8_t* buffer,uint32_t size);
  static bool onStartStop(uint8_t powerCondition,bool start,bool loadEject);
  int32_t transfer(bool write,uint32_t lba,uint32_t offset,void* buffer,uint32_t size);
  void endHostOwnership(bool disconnected);
};
#else
class UsbMassStorageService {public:void begin(){}void loop(){}bool busy()const{return false;}};
#endif
extern UsbMassStorageService usbStorage;
