#include "UsbMassStorage.h"
#include "Library.h"
#include <SD.h>
#include "AudioEngine.h"
#include "SyncMode.h"
#include "IpodIdentity.h"
#include "Settings.h"
#include "UI.h"
#include "WiFiSync.h"
#include <SPI.h>
#if MONOPOD_EXPERIMENTAL_USB_MSC
#include <tusb.h>
#endif
UsbMassStorageService usbStorage;
#if MONOPOD_EXPERIMENTAL_USB_MSC
UsbMassStorageService* UsbMassStorageService::instance=nullptr;
namespace {
void ensureIpodStructure(){
  // Minimal 2004-era FAT layout. Never replace a host-owned iTunesDB.
  if(!SD.exists("/iPod_Control"))SD.mkdir("/iPod_Control");
  if(!SD.exists("/iPod_Control/Device"))SD.mkdir("/iPod_Control/Device");
  if(!SD.exists("/iPod_Control/iTunes"))SD.mkdir("/iPod_Control/iTunes");
  if(!SD.exists("/iPod_Control/Music"))SD.mkdir("/iPod_Control/Music");
  if(SD.exists("/iPod_Control/Device/SysInfo"))return;
  File f=SD.open("/iPod_Control/Device/SysInfo",FILE_WRITE);
  if(!f)return;
  f.println("BoardHwName: iPod Q21");
  f.println("ModelNumStr: M9282");
  f.print("FirewireGuid: 0x");f.println(IpodIdentity::guid());
  f.println("iPodFamily: 0x00000004");
  f.println("updaterFamily: 0x00000004");
  f.println("buildID: 0x03118000 (3.1.1)");
  f.println("visibleBuildID: 0x03118000 (3.1.1)");
  f.flush();f.close();
}
// USB callbacks run in the TinyUSB task. They must NOT call UI or FAT FS.
// USB is intentionally presented as removable media; never format on behalf of iTunes.
constexpr uint32_t kBlockSize=512;
}
int32_t UsbMassStorageService::transfer(bool write,uint32_t lba,uint32_t offset,void* buffer,uint32_t size){
 if(!rawCard || !hostHasMedia || !buffer || offset>=kBlockSize)return -1;
 const uint64_t last=(uint64_t)lba*kBlockSize+offset+size;
 if(last>(uint64_t)sectors*kBlockSize)return -1;
 uint8_t* ptr=(uint8_t*)buffer;uint8_t scratch[512];uint32_t left=size;
 while(left){
   const uint32_t take=min((uint32_t)(kBlockSize-offset),left);
   if(offset==0 && take==kBlockSize){
     bool ok=write?rawCard->writeSector(lba,ptr):rawCard->readSector(lba,ptr);
     if(!ok)return -1;
   }else{
     if(!rawCard->readSector(lba,scratch))return -1;
     if(write){memcpy(scratch+offset,ptr,take);if(!rawCard->writeSector(lba,scratch))return -1;}
     else memcpy(ptr,scratch+offset,take);
   }
   ptr+=take;left-=take;lba++;offset=0;
 }
 return size;
}
int32_t UsbMassStorageService::onRead(uint32_t lba,uint32_t offset,void* buffer,uint32_t size){
 return instance?instance->transfer(false,lba,offset,buffer,size):-1;
}
int32_t UsbMassStorageService::onWrite(uint32_t lba,uint32_t offset,uint8_t* buffer,uint32_t size){
 return instance?instance->transfer(true,lba,offset,buffer,size):-1;
}
bool UsbMassStorageService::onStartStop(uint8_t,bool start,bool loadEject){
 if(instance && !start && loadEject)instance->ejectRequested=true;
 return true;
}
void UsbMassStorageService::begin(){
 instance=this;
 // Board USB-OTG only. CDC on boot must be disabled to guarantee descriptors
 // are registered before USB.begin().
#if MONOPOD_EXPERIMENTAL_APPLE_IDENTITY
 if(settings.data.usbMode==UsbMode::ITunes){
    USB.VID(0x05AC);USB.PID(0x1203); // 4th generation grayscale iPod experiment
    USB.manufacturerName("Apple Computer, Inc.");USB.productName("iPod");
 }else
#endif
 {USB.VID(0x303A);USB.PID(0x4001);USB.manufacturerName("MonoPod Project");USB.productName("MonoPod SD Storage");}
 USB.serialNumber(IpodIdentity::guid());
 msc.vendorID(settings.data.usbMode==UsbMode::ITunes?"Apple":"MonoPod");
 msc.productID(settings.data.usbMode==UsbMode::ITunes?"iPod":"SD Storage");
 msc.productRevision("1.0");
 msc.onRead(onRead);msc.onWrite(onWrite);msc.onStartStop(onStartStop);
 msc.isWritable(true);msc.mediaPresent(false);
 // Capacity must be fixed BEFORE enumeration; the S3 class has no dynamic resize.
 advertisedSectorCount=media.sdOK() ? (uint32_t)SD.numSectors() : 1;
 msc.begin(advertisedSectorCount,kBlockSize);
 USB.begin();
 MONOPOD_LOG.println("USB MSC experimental backend initialized; medium not yet exposed");
}
void UsbMassStorageService::endHostOwnership(bool disconnected){
 msc.mediaPresent(false);hostHasMedia=false;
 // No more USB sector transfers may be served after mediaPresent(false).
 delay(10);
 if(rawCard){rawCard->end();rawCard=nullptr;}
 sectors=0;
 if(syncMode.is(SyncTransport::UsbMsc))syncMode.leave(SyncTransport::UsbMsc);
 if(!media.sdOK())media.remount();
 if(media.sdOK())media.rescan(true);
 ui.begin();
 ejectRequested=false;
 ejectedUntilUnplug=!disconnected;
}
void UsbMassStorageService::loop(){
 const bool configured=tud_mounted();
 if(!configured){
   if(hostHasMedia)endHostOwnership(true);
   ejectedUntilUnplug=false;lastConfigured=false;noSdWarned=false;return;
 }
 if(ejectRequested){if(hostHasMedia)endHostOwnership(false);else ejectedUntilUnplug=true;return;}
 if(ejectedUntilUnplug || hostHasMedia)return;
 if(syncMode.active() || wifiSync.active())return;
 if(!media.sdOK()){
   if(!noSdWarned){ui.notify("Insert microSD for USB");noSdWarned=true;}
   return;
 }
 if(settings.data.usbMode==UsbMode::ITunes)ensureIpodStructure();
 // Release FAT/VFS ownership BEFORE giving the host block-level access.
 // The display is refreshed once before MSC becomes ready, then frozen.
 if(!syncMode.enter(SyncTransport::UsbMsc))return;
 ui.loop();
 media.eject();
 rawCard=rawFactory.newCard(SdSpiConfig(Config::PIN_SD_CS,SHARED_SPI,SD_SCK_MHZ(4),&SPI));
 if(!rawCard || rawCard->errorCode() || !(sectors=rawCard->sectorCount())){
   if(rawCard)rawCard->end();rawCard=nullptr;sectors=0;
   syncMode.leave(SyncTransport::UsbMsc);media.remount();ui.begin();
   ui.notify("SD error: USB unavailable");return;
 }
 // USBMSC capacity was advertised at boot, so reject hot-swapped cards.
 if(sectors!=advertisedSectorCount){
   rawCard->end();rawCard=nullptr;sectors=0;syncMode.leave(SyncTransport::UsbMsc);
   media.remount();ui.begin();ui.notify("Restart with this SD");return;
 }
 hostHasMedia=true;msc.mediaPresent(true);
 lastConfigured=true;
 MONOPOD_LOG.printf("USB MSC: %lu sectors, %llu bytes, mode=%u\n",
      (unsigned long)sectors,(unsigned long long)sectors*512ULL,(unsigned)settings.data.usbMode);
}
#endif
