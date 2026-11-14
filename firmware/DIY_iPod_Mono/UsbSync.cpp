#include "UsbSync.h"
#include "Library.h"
#include "AudioEngine.h"
#include "WiFiSync.h"
#include "UI.h"
#include "IpodIdentity.h"
#include "SyncMode.h"
#include <stdlib.h>
#include <ctype.h>

UsbSyncService usbSync;
#if FEATURE_USB_SYNC
namespace {
constexpr uint32_t MAX_FILE_BYTES=1024UL*1024UL*1024UL;
constexpr uint16_t CHUNK_BYTES=1024;
constexpr uint32_t RECEIVE_TIMEOUT_MS=15000;
constexpr char PART_PATH[]="/Music/.monopod-upload.part";
constexpr char BACKUP_PATH[]="/Music/.monopod-previous.bak";
constexpr char JOURNAL_PATH[]="/Music/.monopod-target.txt";

uint32_t crc32Step(uint32_t crc, uint8_t byte) {
  crc^=byte;
  for(uint8_t i=0;i<8;i++) crc=(crc>>1)^((crc&1)?0xEDB88320UL:0);
  return crc;
}
bool parseUint(const String& s, uint32_t& output, int base) {
  if(s.isEmpty())return false;
  for(unsigned i=0;i<s.length();i++){
    const char c=s[i];
    if(base==10 && (c<'0'||c>'9'))return false;
    if(base==16 && !isxdigit((unsigned char)c))return false;
  }
  char *end=nullptr;
  unsigned long v=strtoul(s.c_str(),&end,base);
  if(*end || (base==16 && s.length()!=8))return false;
  output=(uint32_t)v;
  return true;
}
bool validName(const String& name) {
  if(name.length()<5||name.length()>90||name[0]=='.'||name.endsWith("."))return false;
  if(name.indexOf("..")>=0)return false;
  for(unsigned i=0;i<name.length();i++){
    uint8_t c=(uint8_t)name[i];
    if(c<32||c==127||c=='/'||c=='\\'||c==':'||c=='<'||c=='>'||c=='"'||c=='|'||c=='*'||c=='?')return false;
  }
  return MediaLibrary::isAudio(name);
}
String destPath(const String& name){return String("/Music/")+name;}
// Startup recovery for power-loss between original->backup and upload->original.
void recoverReplacement(){
  if(!SD.exists(JOURNAL_PATH))return;
  File j=SD.open(JOURNAL_PATH,FILE_READ);
  String name=j?j.readStringUntil('\n'):String();
  if(j)j.close();name.trim();
  if(!validName(name))return; // do not touch unknown paths
  String dest=destPath(name);
  if(SD.exists(BACKUP_PATH)){
    if(SD.exists(dest.c_str()))SD.remove(BACKUP_PATH); // new file committed
    else SD.rename(BACKUP_PATH,dest.c_str());          // old file restored
  }
  if(!SD.exists(BACKUP_PATH))SD.remove(JOURNAL_PATH);
}
}
void UsbSyncService::begin(){
  line.reserve(180);
  if(media.sdOK()){
    const bool interrupted=SD.exists(JOURNAL_PATH);
    recoverReplacement();
    SD.remove(PART_PATH);
    if(interrupted)media.rescan();
  }
}
void UsbSyncService::reply(const String& payload){Serial.print("MPOD1 ");Serial.println(payload);}
void UsbSyncService::reset(bool removePart){
  if(upload)upload.close();
  if(removePart && media.sdOK())SD.remove(PART_PATH);
  sending=false;replacing=false;size=received=0;chunkRemaining=buffered=0;
  fileName="";expectedCrc=0;receivedCrc=0xFFFFFFFFUL;
}
void UsbSyncService::fail(const char* reason){reset(true);reply(String("ERR ")+reason);}
bool UsbSyncService::flushBuffer(){
  if(!buffered)return true;
  const size_t written=upload.write(buffer,buffered);
  if(written!=buffered)return false;
  buffered=0;return true;
}
void UsbSyncService::finish(){
  if(!flushBuffer()){fail("SD_WRITE");return;}
  upload.flush();upload.close();
  if((receivedCrc^0xFFFFFFFFUL)!=expectedCrc){fail("CRC_MISMATCH");return;}
  const String dest=destPath(fileName);
  const bool exists=SD.exists(dest.c_str());
  if(exists && !replacing){fail("ALREADY_EXISTS");return;}
  if(exists){
    if(SD.exists(JOURNAL_PATH)||SD.exists(BACKUP_PATH)){
      fail("BACKUP_PENDING");return;
    }
    File j=SD.open(JOURNAL_PATH,FILE_WRITE);
    if(!j){fail("JOURNAL_WRITE");return;}
    j.println(fileName);j.flush();j.close();
    if(!SD.rename(dest.c_str(),BACKUP_PATH)){
      SD.remove(JOURNAL_PATH);fail("BACKUP_RENAME");return;
    }
  }
  if(!SD.rename(PART_PATH,dest.c_str())){
    if(exists){SD.rename(BACKUP_PATH,dest.c_str());
      if(!SD.exists(BACKUP_PATH))SD.remove(JOURNAL_PATH);}
    fail("COMMIT_FAILED");return;
  }
  if(exists){SD.remove(BACKUP_PATH);SD.remove(JOURNAL_PATH);}
  reset(false);
  reply(exists?"OK REPLACED":"OK SAVED");
}
void UsbSyncService::command(const String& cmd){
  if(cmd=="MPOD1 HELLO"){
    if(!media.sdOK())ui.notify("No microSD card");
    reply(String("READY 3 ")+(media.sdOK()?"SD=1":"SD=0"));return;
  }
  if(cmd=="MPOD1 INFO"){
    char totalDec[24],freeDec[24];
    snprintf(totalDec,sizeof(totalDec),"%llu",(unsigned long long)media.totalBytes());
    snprintf(freeDec,sizeof(freeDec),"%llu",(unsigned long long)(media.totalBytes()-media.usedBytes()));
    String answer=String("OK INFO SD=")+(media.sdOK()?"1":"0")+
      " TRACKS="+String((unsigned)media.size())+
      " TOTAL_BYTES="+String(totalDec)+
      " FREE_BYTES="+String(freeDec)+
      " GUID="+String(IpodIdentity::guid());
    reply(answer);return;
  }
  if(cmd=="MPOD1 SYNC START"){
    if(!media.sdOK()){
      ui.notify("No microSD card");reply("ERR NO_SD");return;
    }
    if(syncMode.active() && !syncMode.is(SyncTransport::UsbCdc)){
      reply("ERR SYNC_BUSY");return;
    }
    if(!syncMode.enter(SyncTransport::UsbCdc)){
      reply("ERR SYNC_BUSY");return;
    }
    reply("OK SYNC_STARTED");return;
  }
  if(cmd=="MPOD1 SYNC END"){
    if(!syncMode.is(SyncTransport::UsbCdc)){
      reply("ERR NO_USB_SYNC");return;
    }
    if(sending){reply("ERR TRANSFER_ACTIVE");return;}
    media.rescan();
    syncMode.leave(SyncTransport::UsbCdc);
    ui.begin();
    reply("OK SYNC_ENDED");return;
  }
  // USB connection and INFO requests are not permission to touch the SD.
  if(!syncMode.canWrite(SyncTransport::UsbCdc)){
    reply("ERR SYNC_REQUIRED");return;
  }
  if(cmd=="MPOD1 SCAN"){
    if(!media.sdOK()){reply("ERR NO_SD");return;}
    if(wifiSync.active()){reply("ERR WIFI_ACTIVE");return;}
    media.rescan();ui.redraw();
    reply(String("OK SCANNED ")+(unsigned)media.size());return;
  }
  if(cmd.startsWith("MPOD1 DELETE ")){
    if(!media.sdOK()){reply("ERR NO_SD");return;}
    if(wifiSync.active()){reply("ERR WIFI_ACTIVE");return;}
    const String name=cmd.substring(13);
    if(!validName(name)){reply("ERR INVALID_FILE");return;}
    const String target=destPath(name);
    if(!SD.exists(target.c_str())){reply("ERR NOT_FOUND");return;}
    player.stop();
    if(!SD.remove(target.c_str())){reply("ERR DELETE_FAILED");return;}
    reply("OK DELETED");return;
  }
  const bool replace=cmd.startsWith("MPOD1 REPLACE ");
  if(!replace && !cmd.startsWith("MPOD1 PUT ")){reply("ERR UNKNOWN_COMMAND");return;}
  if(!media.sdOK()){reply("ERR NO_SD");return;}
  if(wifiSync.active()){reply("ERR WIFI_ACTIVE");return;}
  // MPOD1 PUT|REPLACE <decimal-size> <8-char-hex-crc32> <filename>
  const int prefix=replace?14:10;
  const int p1=cmd.indexOf(' ',prefix),p2=p1>=0?cmd.indexOf(' ',p1+1):-1;
  if(p1<0||p2<0){reply("ERR ARGUMENTS");return;}
  const String lenStr=cmd.substring(prefix,p1);
  const String crcStr=cmd.substring(p1+1,p2);
  const String name=cmd.substring(p2+1);
  uint32_t n=0,crc=0;
  if(!parseUint(lenStr,n,10)||!parseUint(crcStr,crc,16)||n==0||n>MAX_FILE_BYTES||!validName(name)){
    reply("ERR INVALID_FILE");return;
  }
  if(SD.exists(destPath(name).c_str())&&!replace){reply("ERR ALREADY_EXISTS");return;}
  if(SD.exists(JOURNAL_PATH)||SD.exists(BACKUP_PATH)){
    recoverReplacement();
    if(SD.exists(JOURNAL_PATH)||SD.exists(BACKUP_PATH)){reply("ERR BACKUP_PENDING");return;}
  }
  uint64_t freeBytes=media.totalBytes()-media.usedBytes();
  // Replacement needs free space for the NEW file while the old one is intact.
  // Small filesystem safety margin avoids writing through the last clusters.
  if(freeBytes<(uint64_t)n+4096ULL){reply("ERR NO_SPACE");return;}
  player.stop();
  if(!SD.exists("/Music") && !SD.mkdir("/Music")){reply("ERR MKDIR");return;}
  SD.remove(PART_PATH);
  upload=SD.open(PART_PATH,FILE_WRITE);
  if(!upload){reply("ERR OPEN_FAILED");return;}
  fileName=name;size=n;expectedCrc=crc;received=0;receivedCrc=0xFFFFFFFFUL;
  buffered=0;sending=true;replacing=replace;lastByteAt=millis();
  chunkRemaining=(uint16_t)min((uint32_t)CHUNK_BYTES,size);
  reply(String("NEXT ")+chunkRemaining);
}
void UsbSyncService::loop(){
  if(sending && millis()-lastByteAt>RECEIVE_TIMEOUT_MS){fail("TIMEOUT");return;}
  uint16_t budget=256;
  while(budget-- && Serial.available()>0){
    const int v=Serial.read();if(v<0)break;
    if(sending){
      const uint8_t byte=(uint8_t)v;
      buffer[buffered++]=byte;
      receivedCrc=crc32Step(receivedCrc,byte);
      received++;chunkRemaining--;lastByteAt=millis();
      if(buffered==sizeof(buffer)&&!flushBuffer()){fail("SD_WRITE");return;}
      if(chunkRemaining==0){
        if(!flushBuffer()){fail("SD_WRITE");return;}
        if(received>=size)finish();
        else {chunkRemaining=(uint16_t)min((uint32_t)CHUNK_BYTES,size-received);
          reply(String("NEXT ")+chunkRemaining);}
        return;
      }
    }else{
      if(v=='\n'){
        String cmd=line;line="";cmd.trim();
        if(!cmd.isEmpty())command(cmd);
        return;
      }else if(v=='\r')continue;
      else if(line.length()<175 && v>=32 && v<256)line+=(char)v;
      else {line="";reply("ERR LINE_TOO_LONG");return;}
    }
  }
}
#endif
