#include "UsbSync.h"
#include "SyncFiles.h"
#include "Library.h"
#include "UI.h"
#include "Settings.h"
#include "IpodIdentity.h"
#include "WiFiSync.h"
#include <errno.h>
UsbSyncService usbSync;
#if FEATURE_USB_SYNC
namespace {
bool uintArg(const String& s,uint32_t& v,int base=10){if(s.isEmpty()||s.length()>10)return false;for(size_t i=0;i<s.length();i++)if(base==10? !isdigit((unsigned char)s[i]):!isxdigit((unsigned char)s[i]))return false;errno=0;char* end;unsigned long n=strtoul(s.c_str(),&end,base);if(errno||*end)return false;v=n;return true;}
String token(const String& s,int& i){if(i>=(int)s.length())return "";int end=s.indexOf(' ',i);if(end<0)end=s.length();String t=s.substring(i,end);i=end+1;return t;}
}
void UsbSyncService::begin(){line.reserve(700);SyncFiles::recover();}
void UsbSyncService::reply(const String& s){Serial.print("MPOD1 ");Serial.println(s);}
void UsbSyncService::fail(const char* why){SyncFiles::abort();sending=false;buffered=chunkRemaining=0;line="";discard=true;quietUntil=millis()+1500;reply(String("ERR ")+why);}
void UsbSyncService::finish(){if(buffered&&!SyncFiles::write(buffer,buffered)){fail(SyncFiles::error.c_str());return;}buffered=0;sending=false;if(!SyncFiles::commit()){String error=SyncFiles::error;SyncFiles::abort();reply("ERR "+error);return;}reply("OK SAVED");}
void UsbSyncService::command(const String& cmd){
 if(cmd=="MPOD1 HELLO"){reply(String("READY 4 SD=")+(media.sdOK()?"1":"0"));return;}
 if(cmd=="MPOD1 X INFO"||cmd=="MPOD1 X STORAGE"){reply("OK "+SyncFiles::info());return;}
 if(cmd=="MPOD1 INFO"){char a[24],b[24];snprintf(a,sizeof(a),"%llu",(unsigned long long)media.totalBytes());snprintf(b,sizeof(b),"%llu",(unsigned long long)(media.totalBytes()-media.usedBytes()));reply(String("OK INFO SD=")+(media.sdOK()?"1":"0")+" TRACKS="+String((unsigned)media.size())+" TOTAL_BYTES="+a+" FREE_BYTES="+b+" GUID="+IpodIdentity::guid()+" FW=0.10.0 PROTOCOL=4");return;}
 if(cmd=="MPOD1 SYNC START"){if(!media.sdOK()){reply("ERR NO_SD");return;}reply(syncMode.enter(SyncTransport::UsbCdc)?"OK SYNC_STARTED":"ERR SYNC_BUSY");return;}
 if(cmd=="MPOD1 SYNC END"){if(sending||SyncFiles::busy()){reply("ERR TRANSFER_ACTIVE");return;}if(!syncMode.is(SyncTransport::UsbCdc)){reply("ERR NO_USB_SYNC");return;}media.rescan();syncMode.leave(SyncTransport::UsbCdc);ui.begin();reply("OK SYNC_ENDED");return;}
 if(!SyncFiles::gate(SyncTransport::UsbCdc)){reply("ERR "+SyncFiles::error);return;}
 if(cmd=="MPOD1 SCAN"){if(SyncFiles::busy()){reply("ERR TRANSFER_ACTIVE");return;}media.rescan();reply("OK SCANNED "+String((unsigned)media.size()));return;}
 if(cmd.startsWith("MPOD1 X ")){
  int i=8;String op=token(cmd,i),path=SyncFiles::fromHex(token(cmd,i));
  if(op=="SMOOTH"){String value=cmd.substring(15);if(value!="0" && value!="1"){reply("ERR ARGUMENTS");return;}settings.data.fontSmoothing=(value=="1");settings.markDirty();reply("OK SMOOTHING");return;}
  if(op=="LIST"){uint32_t off=0,limit=0;if(!uintArg(token(cmd,i),off)||!uintArg(token(cmd,i),limit)||limit>32){reply("ERR ARGUMENTS");return;}String result=SyncFiles::list(path,off,limit,SyncTransport::UsbCdc);reply(result.length()?"OK "+result:"ERR "+SyncFiles::error);return;}
  if(op=="STAT"||op=="META"||op=="THEMEINFO"){String result=SyncFiles::stat(op=="THEMEINFO"?"/MonoPod/theme.mpt":path,SyncTransport::UsbCdc);reply(result.length()?"OK "+result:"ERR "+SyncFiles::error);return;}
  if(op=="READ"||op=="GET"){uint32_t off=0,count=0;if(!uintArg(token(cmd,i),off)||!uintArg(token(cmd,i),count)||count>1024){reply("ERR ARGUMENTS");return;}size_t got=0;if(!SyncFiles::read(path,off,buffer,count,got,SyncTransport::UsbCdc)){reply("ERR "+SyncFiles::error);return;}char crc[9];snprintf(crc,sizeof(crc),"%08lX",(unsigned long)~SyncFiles::crcStep(~0u,buffer,got));reply("DATA "+String(got)+" "+crc);Serial.write(buffer,got);return;}
  if(op=="MKDIR"||op=="DELETE"||op=="RMDIR"||op=="RENAME"){
   bool ok=op=="MKDIR"?SyncFiles::mkdir(path,SyncTransport::UsbCdc):op=="RENAME"?SyncFiles::rename(path,SyncFiles::fromHex(token(cmd,i)),SyncTransport::UsbCdc):SyncFiles::remove(path,op=="RMDIR",SyncTransport::UsbCdc);reply(ok?"OK DONE":"ERR "+SyncFiles::error);return;
  }
  if(op=="PUT"||op=="THEME"){
   uint32_t n=0,crc=0,replace=0;String len=token(cmd,i),crcText=token(cmd,i),rep=token(cmd,i);
   if(!uintArg(len,n)||crcText.length()!=8||!uintArg(crcText,crc,16)||!uintArg(rep,replace)||replace>1){reply("ERR ARGUMENTS");return;}
   if(!SyncFiles::begin(path,n,crc,replace,op=="THEME",SyncTransport::UsbCdc)){reply("ERR "+SyncFiles::error);return;}
   size=n;received=0;buffered=0;sending=true;lastByteAt=millis();chunkRemaining=min((uint32_t)sizeof(buffer),size);if(!size){finish();return;}reply("NEXT "+String(chunkRemaining));return;
  }
  reply("ERR UNKNOWN_COMMAND");return;
 }
 // Legacy v1-v3 commands remain byte-compatible and target /Music only.
 if(cmd.startsWith("MPOD1 DELETE ")){String name=cmd.substring(13),path="/Music/"+name;if(name.indexOf('/')>=0||!MediaLibrary::isAudio(name)){reply("ERR INVALID_FILE");return;}reply(SyncFiles::remove(path,false,SyncTransport::UsbCdc)?"OK DELETED":"ERR "+SyncFiles::error);return;}
 bool replace=cmd.startsWith("MPOD1 REPLACE ");if(replace||cmd.startsWith("MPOD1 PUT ")){
  int i=replace?14:10;uint32_t n=0,crc=0;String len=token(cmd,i),crcText=token(cmd,i),name=cmd.substring(i);
  if(!uintArg(len,n)||!n||crcText.length()!=8||!uintArg(crcText,crc,16)||name.indexOf('/')>=0||!MediaLibrary::isAudio(name)){reply("ERR INVALID_FILE");return;}
  if(!SD.exists("/Music"))SD.mkdir("/Music");if(!SyncFiles::begin("/Music/"+name,n,crc,replace,false,SyncTransport::UsbCdc)){reply("ERR "+SyncFiles::error);return;}
  size=n;received=0;buffered=0;sending=true;chunkRemaining=min((uint32_t)sizeof(buffer),size);lastByteAt=millis();reply("NEXT "+String(chunkRemaining));return;
 }
 reply("ERR UNKNOWN_COMMAND");
}
void UsbSyncService::loop(){
 if(sending && (!Serial || (uint32_t)(millis()-lastByteAt)>15000)){fail("TIMEOUT_OR_DISCONNECT");return;}
 uint16_t budget=1024;while(budget-- && Serial.available()){
  int c=Serial.read();if(c<0)break;
  if(discard){quietUntil=millis()+1500;continue;}
  if(sending){buffer[buffered++]=(uint8_t)c;received++;chunkRemaining--;lastByteAt=millis();if(!chunkRemaining){if(!SyncFiles::write(buffer,buffered)){fail(SyncFiles::error.c_str());return;}buffered=0;if(received==size)finish();else{chunkRemaining=min((uint32_t)sizeof(buffer),size-received);reply("NEXT "+String(chunkRemaining));}}}
  else if(c=='\n'){String cmd=line;line="";if(cmd.startsWith("MPOD1 "))command(cmd);}
  else if(c!='\r'){if(line.length()>=700){line="";discard=true;quietUntil=millis()+1500;reply("ERR LINE_TOO_LONG");}else line+=(char)c;}
 }
 if(discard && (int32_t)(millis()-quietUntil)>=0){discard=false;line="";}
}
#endif

