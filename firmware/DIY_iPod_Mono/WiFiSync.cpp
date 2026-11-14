#include "WiFiSync.h"
#include "UsbSync.h"
#include "SyncFiles.h"
#include "WiFiNetworks.h"
#include "Library.h"
#include "Display.h"
#include "Theme.h"
#include "UI.h"
#include "Settings.h"
#include "ClockService.h"
#include "ClockConfigValidation.h"
#include "MonoPodDB.h"
#include <WiFi.h>
#include <ESPmDNS.h>
WiFiSyncService wifiSync;
#if FEATURE_WIFI_SYNC
namespace {
bool number(const String& s,uint32_t& value,int base=10){if(s.isEmpty()||s.length()>10)return false;for(size_t i=0;i<s.length();i++)if(base==10?!isdigit((unsigned char)s[i]):!isxdigit((unsigned char)s[i]))return false;unsigned long long v=strtoull(s.c_str(),nullptr,base);if(v>0xffffffffULL)return false;value=v;return true;}
}
String WiFiSyncService::ip()const{return running?(usingSta?WiFi.localIP():WiFi.softAPIP()).toString():"Offline";}
bool WiFiSyncService::busy()const{return uploadActive||SyncFiles::busy()||wifiOta.busy();}
bool WiFiSyncService::allowed(){if(!sessionAuthorized()){server.send(403,"text/plain","SESSION_DENIED");return false;}if(wifiOta.busy()){server.send(409,"text/plain","OTA_ACTIVE");return false;}if(!SyncFiles::gate(SyncTransport::WiFi)){server.send(409,"text/plain",SyncFiles::error);return false;}return true;}
void WiFiSyncService::result(bool ok){if(ok)server.send(200,"application/json","{\"ok\":true}");else{String e=SyncFiles::error;int code=e=="NOT_FOUND"?404:e=="SD_WRITE"||e=="SD_READ"?500:e=="NO_SPACE"?507:409;server.send(code,"text/plain",e);}}
void WiFiSyncService::upload(){
 HTTPUpload& u=server.upload();
 if(u.status==UPLOAD_FILE_START){
  if(uploadActive){uploadError="Only one multipart file is allowed";SyncFiles::abort();return;}
  uploadActive=true;uploadOk=false;uploadError="";
  if(wifiOta.busy()){uploadError="OTA_ACTIVE";return;}
  if(!sessionAuthorized()){uploadError="SESSION_DENIED";return;}
  if(!SyncFiles::gate(SyncTransport::WiFi)){uploadError=SyncFiles::error;return;}
  bool theme=server.uri()=="/api/theme";
  String path=theme?"/MonoPod/theme.mpt":server.arg("path");uint32_t size=0,crc=0;
  if(!number(server.arg("size"),size)||server.arg("crc").length()!=8||!number(server.arg("crc"),crc,16)){
    uploadError="INVALID_ARGUMENTS";return;
  }
  if(!SyncFiles::begin(path,size,crc,server.arg("replace")=="1",theme,SyncTransport::WiFi))uploadError=SyncFiles::error;
 }else if(u.status==UPLOAD_FILE_WRITE){
  if(uploadError.length())return;if(!SyncFiles::write(u.buf,u.currentSize)){uploadError=SyncFiles::error;SyncFiles::abort();}
 }else if(u.status==UPLOAD_FILE_END){uploadOk=uploadError.isEmpty()&&SyncFiles::busy();}
 else if(u.status==UPLOAD_FILE_ABORTED){SyncFiles::abort();uploadError="ABORTED";uploadActive=false;uploadOk=false;}
}
void WiFiSyncService::finished(){
 bool ok=uploadOk&&uploadError.isEmpty();if(ok&&!SyncFiles::commit()){uploadError=SyncFiles::error;ok=false;}
 if(!ok)SyncFiles::abort();uploadActive=false;uploadOk=false;
 if(ok){server.send(200,"application/json","{\"ok\":true}");ui.redraw();}else server.send(422,"text/plain",uploadError.length()?uploadError:"NO_UPLOAD");
}
void WiFiSyncService::otaUpload(){
 HTTPUpload& u=server.upload();
 if(u.status==UPLOAD_FILE_START){
  otaUploadSeen=true;otaUploadOk=false;
  // Headers are required so multipart form-field order cannot bypass validation.
  otaUploadOk=sessionAuthorized() && wifiOta.start(server.header("X-MonoPod-OTA-PIN"),
    server.header("X-MonoPod-OTA-Size"),server.header("X-MonoPod-OTA-SHA256"),
    uploadActive||SyncFiles::busy()||usbSync.busy());
  ui.redraw();ui.loop();
 }else if(u.status==UPLOAD_FILE_WRITE){
  if(otaUploadOk){
   const uint8_t before=wifiOta.percent();
   otaUploadOk=wifiOta.write(u.buf,u.currentSize);
   if(otaUploadOk && wifiOta.percent()/10!=before/10){ui.redraw();ui.loop();}
  }
 }else if(u.status==UPLOAD_FILE_END){
  // Final verification is in otaFinished, after the multipart stream ends.
 }else if(u.status==UPLOAD_FILE_ABORTED){
  wifiOta.abort("CLIENT_ABORTED");otaUploadOk=false;
 }
}
void WiFiSyncService::otaFinished(){
 if(!otaUploadSeen){server.send(400,"application/json","{\"ok\":false,\"error\":\"NO_FILE\"}");return;}
 bool ok=otaUploadOk && wifiOta.finish();
 const String error=wifiOta.error();
 otaUploadSeen=false;otaUploadOk=false;
 if(ok){
  server.sendHeader("Connection","close");
  server.send(200,"application/json","{\"ok\":true,\"status\":\"verified\",\"rebooting\":true}");
 }else{
  if(wifiOta.inProgress()&&!wifiOta.restartPending())wifiOta.abort("INCOMPLETE_IMAGE");
  int status=error=="INVALID_PIN"||error=="PIN_LOCKED_RESTART_SYNC"?403:
    error=="NO_OTA_PARTITION"?409:error=="LOW_SUPPLY_OR_BUTTON_HELD"?409:error=="TRANSFER_ACTIVE"?409:422;
  server.send(status,"application/json",String("{\"ok\":false,\"error\":\"")+error+"\"}");
 }
 ui.redraw();
}
bool WiFiSyncService::sessionAuthorized() {
  if(!syncModeActive)return false;
  if(!autoSession)return true; // Legacy explicit session remains compatible.
  return server.client().remoteIP()==requestIP &&
    sessionToken.length() && server.header("X-MonoPod-Session")==sessionToken;
}
void WiFiSyncService::startListener(){
  if(running || WiFi.status()!=WL_CONNECTED || syncMode.active())return;
  if(!routesReady){routes();routesReady=true;}
  usingSta=true;server.begin();running=true;syncModeActive=false;
  if(MDNS.begin("monopod")){
    MDNS.addService("monopod","tcp",80);
    MDNS.addServiceTxt("monopod","tcp","protocol","4");
    MDNS.addServiceTxt("monopod","tcp","consent","required");
  }
  MONOPOD_LOG.printf("[Wi-Fi] Sync discovery listening at http://%s:80\n",ip().c_str());
}
void WiFiSyncService::stopListener(){
  if(!running || syncModeActive)return;
  server.stop();MDNS.end();running=false;waitingForConsent=false;requestApproved=false;
}
void WiFiSyncService::answer(bool yes){
  if(!waitingForConsent)return;
  waitingForConsent=false;requestDenied=!yes;requestApproved=false;
  if(yes && !syncMode.active() && media.sdOK() && syncMode.enter(SyncTransport::WiFi)){
    syncModeActive=true;autoSession=true;requestApproved=true;requestDenied=false;
    sessionToken=String((uint32_t)esp_random(),HEX)+String((uint32_t)esp_random(),HEX);
    wifiOta.newSession();
    MONOPOD_LOG.println("[Wi-Fi Sync] Consent accepted");
  }
  ui.redraw();
}
void WiFiSyncService::routes(){
 // The listener is harmless until user approves a particular desktop.
 server.on("/api/sync/request",HTTP_POST,[this](){
   if(WiFi.status()!=WL_CONNECTED || syncMode.active() || !media.sdOK()){
     server.send(409,"application/json","{\"error\":\"NOT_AVAILABLE\"}");return;
   }
   if(waitingForConsent){server.send(409,"application/json","{\"error\":\"PENDING\"}");return;}
   requestIP=server.client().remoteIP();requestName=server.arg("client");
   if(requestName.isEmpty())requestName="MonoPod Desktop";
   if(requestName.length()>28)requestName=requestName.substring(0,28);
   for(size_t i=0;i<requestName.length();i++)if((unsigned char)requestName[i]<32)requestName.setCharAt(i,' ');
   waitingForConsent=true;requestApproved=false;requestDenied=false;requestSince=millis();
   server.send(202,"application/json","{\"state\":\"pending\",\"timeout_seconds\":30}");ui.redraw();
 });
 server.on("/api/sync/status",HTTP_GET,[this](){
   if(server.client().remoteIP()!=requestIP){server.send(403,"text/plain","NOT_REQUESTER");return;}
   if(waitingForConsent){server.send(200,"application/json","{\"state\":\"pending\"}");return;}
   if(requestApproved && syncModeActive){
     server.send(200,"application/json",String("{\"state\":\"approved\",\"session\":\"")+sessionToken+"\"}");return;
   }
   server.send(200,"application/json",requestDenied?"{\"state\":\"denied\"}":"{\"state\":\"idle\"}");
 });
 // Custom request headers are not exposed unless explicitly collected.
 const char* otaHeaders[]={"X-MonoPod-OTA-PIN","X-MonoPod-OTA-Size","X-MonoPod-OTA-SHA256","X-MonoPod-Session"};
 server.collectHeaders(otaHeaders,4);
 server.on("/api/ota/info",HTTP_GET,[this](){if(allowed())server.send(200,"application/json",wifiOta.info());});
 server.on("/api/ota/upload",HTTP_POST,[this](){otaFinished();},[this](){otaUpload();});
 server.on("/api/info",HTTP_GET,[this](){server.send(200,"application/json",sessionAuthorized()?SyncFiles::info():String("{\"product\":\"MonoPod\",\"sync_protocol\":4,\"requires_consent\":true,\"sync_active\":")+(syncModeActive?"true":"false")+"}");});
 server.on("/api/storage",HTTP_GET,[this](){if(allowed())server.send(200,"application/json",SyncFiles::info());});
 server.on("/api/library/info",HTTP_GET,[this](){
   if(!allowed())return;
   server.send(200,"application/json",String("{\"schema\":1,\"generation\":")+String(media.generation())+
     ",\"count\":"+String((unsigned)media.size())+",\"size\":"+String(MonoPodDB::dbSize())+
     ",\"dirty\":"+(media.dirty()?"true":"false")+"}");
 });
 server.on("/api/library/refresh",HTTP_POST,[this](){
   if(!allowed())return;if(busy()){server.send(409,"text/plain","TRANSFER_ACTIVE");return;}
   // Force sampled CRC even if timestamp/size match (metadata edits).
   media.rescan(true);
   if(media.dirty()){server.send(500,"text/plain","DB_WRITE_FAILED");return;}
   server.send(200,"application/json",String("{\"ok\":true,\"generation\":")+String(media.generation())+
     ",\"count\":"+String((unsigned)media.size())+"}");
 });
 server.on("/api/library/db",HTTP_GET,[this](){
   if(!allowed())return;
   if(media.dirty()||!media.indexReady()){server.send(409,"text/plain","DB_STALE_REFRESH_REQUIRED");return;}
   uint32_t offset=0,count=0;
   if(!number(server.arg("offset"),offset)||!number(server.arg("count"),count)||count>16384){server.send(400,"text/plain","INVALID_RANGE");return;}
   File db=SD.open(MonoPodDB::PATH,FILE_READ);
   if(!db){server.send(500,"text/plain","DB_READ");return;}
   const uint32_t size=db.size();if(offset>size){db.close();server.send(416,"text/plain","INVALID_RANGE");return;}
   const uint32_t total=min(count,size-offset);uint32_t crc=~0u;
   uint8_t chunk[512];
   if(!db.seek(offset)){db.close();server.send(500,"text/plain","DB_SEEK");return;}
   for(uint32_t sent=0;sent<total;){
     const size_t step=min((uint32_t)sizeof(chunk),total-sent);
     const size_t got=db.read(chunk,step);
     if(got!=step){db.close();server.send(500,"text/plain","DB_READ");return;}
     crc=MonoPodDB::crcStep(crc,chunk,got);sent+=got;
   }
   if(!db.seek(offset)){db.close();server.send(500,"text/plain","DB_SEEK");return;}
   server.sendHeader("X-CRC32",MonoPodDB::hex32(~crc));
   server.sendHeader("X-DB-Generation",String(media.generation()));
   server.setContentLength(total);server.send(200,"application/octet-stream","");
   for(uint32_t sent=0;sent<total;){
     const size_t step=min((uint32_t)sizeof(chunk),total-sent);
     const size_t got=db.read(chunk,step);
     if(got!=step)break;
     if(server.client().write(chunk,got)!=got)break;sent+=got;yield();
   }
   db.close();
 });
 server.on("/api/fs/list",HTTP_GET,[this](){if(!allowed())return;uint32_t offset=0,limit=32;if((server.hasArg("offset")&&!number(server.arg("offset"),offset))||(server.hasArg("limit")&&!number(server.arg("limit"),limit))||limit>32||!limit){server.send(400,"text/plain","INVALID_ARGUMENTS");return;}String json=SyncFiles::list(server.arg("path"),offset,limit,SyncTransport::WiFi);if(json.length())server.send(200,"application/json",json);else result(false);});
 auto stat=[this](){if(!allowed())return;String json=SyncFiles::stat(server.arg("path"),SyncTransport::WiFi);if(json.length())server.send(200,"application/json",json);else result(false);};
 server.on("/api/fs/stat",HTTP_GET,stat);server.on("/api/fs/metadata",HTTP_GET,stat);
 server.on("/api/theme/info",HTTP_GET,[this](){if(!allowed())return;String json=SyncFiles::stat("/MonoPod/theme.mpt",SyncTransport::WiFi);if(json.length())server.send(200,"application/json",json);else result(false);});
 server.on("/api/fs/download",HTTP_GET,[this](){
  if(!allowed())return;uint32_t offset=0,count=0;if(!number(server.arg("offset"),offset)||!number(server.arg("count"),count)||count>16384){server.send(400,"text/plain","INVALID_RANGE");return;}
  // Fixed 1 KiB working buffer; two passes compute CRC before streaming the same range.
  uint8_t buffer[1024];size_t total=0,got=0;uint32_t crc=~0u;
  while(total<count){size_t n=min((size_t)1024,(size_t)(count-total));if(!SyncFiles::read(server.arg("path"),offset+total,buffer,n,got,SyncTransport::WiFi)){result(false);return;}crc=SyncFiles::crcStep(crc,buffer,got);total+=got;if(got<n)break;}
  char text[9];snprintf(text,sizeof(text),"%08lX",(unsigned long)~crc);server.sendHeader("X-CRC32",text);server.setContentLength(total);server.send(200,"application/octet-stream","");size_t sent=0;
  while(sent<total){size_t n=min((size_t)1024,total-sent);if(!SyncFiles::read(server.arg("path"),offset+sent,buffer,n,got,SyncTransport::WiFi)||!got)break;if(server.client().write(buffer,got)!=got)break;sent+=got;yield();}
 });
 server.on("/api/fs/mkdir",HTTP_POST,[this](){if(allowed())result(SyncFiles::mkdir(server.arg("path"),SyncTransport::WiFi));});
 server.on("/api/fs/rename",HTTP_POST,[this](){if(allowed())result(SyncFiles::rename(server.arg("path"),server.arg("destination"),SyncTransport::WiFi));});
 server.on("/api/fs/file",HTTP_DELETE,[this](){if(allowed())result(SyncFiles::remove(server.arg("path"),server.arg("directory")=="1",SyncTransport::WiFi));});
 server.on("/api/fs/upload",HTTP_POST,[this](){finished();},[this](){upload();});
 server.on("/api/theme",HTTP_POST,[this](){finished();},[this](){upload();});
 server.on("/api/smoothing",HTTP_POST,[this](){if(!allowed())return;if(!server.hasArg("enabled")){server.send(400,"text/plain","Missing enabled");return;}settings.data.fontSmoothing=server.arg("enabled")=="1";settings.markDirty();result(true);});
 server.on("/api/theme/reset",HTTP_POST,[this](){if(!allowed())return;if(busy()){server.send(409,"text/plain","TRANSFER_ACTIVE");return;}if(SD.exists("/MonoPod/theme.mpt")&&!SD.remove("/MonoPod/theme.mpt")){server.send(500,"text/plain","DELETE_FAILED");return;}theme.unload();display.palette(settings.data.contrast);result(true);});
 server.on("/api/scan",HTTP_POST,[this](){if(!allowed())return;if(busy()){server.send(409,"text/plain","TRANSFER_ACTIVE");return;}media.rescan(true);if(media.dirty())server.send(500,"text/plain","DB_WRITE_FAILED");else result(true);});
 server.on("/api/sync/end",HTTP_POST,[this](){if(!allowed())return;if(busy()){server.send(409,"text/plain","TRANSFER_ACTIVE");return;}server.send(200,"application/json","{\"ok\":true}");stopRequested=true;});
 server.on("/api/clock",HTTP_POST,[this](){
   if(!allowed())return;
   if(!server.hasArg("unix") || !server.hasArg("timezone") || !server.hasArg("ntp")){
     server.send(400,"text/plain","Missing unix, timezone or ntp");return;
   }
   const String raw=server.arg("unix");
   if(raw.isEmpty() || raw.length()>11){server.send(400,"text/plain","Invalid unix");return;}
   for(size_t i=0;i<raw.length();i++)if(!isdigit((unsigned char)raw[i])){
     server.send(400,"text/plain","Invalid unix");return;
   }
   const time_t epoch=(time_t)strtoll(raw.c_str(),nullptr,10);
   const String option=server.arg("ntp");
   const String ntpServer=server.hasArg("ntp_server")?server.arg("ntp_server"):clockService.ntpServer();
   if(epoch<1600000000 || epoch>4102444800LL || (option!="0" && option!="1") ||
      !ClockConfigValidation::timezone(server.arg("timezone").c_str()) ||
      !ClockConfigValidation::ntpServer(ntpServer.c_str()) ||
      !clockService.setTimezone(server.arg("timezone")) ||
      !clockService.setNtpServer(ntpServer) ||
      !clockService.setNtpEnabled(option=="1")){
     server.send(400,"text/plain","Invalid clock settings");return;
   }
   clockService.setUnix(epoch);result(true);
  });
 server.onNotFound([this](){server.send(404,"text/plain","Not found");});
}
void WiFiSyncService::begin(){
 if(syncModeActive||usbSync.busy()||syncMode.active())return;if(!media.sdOK()){ui.notify("No microSD card");return;}
 if(WiFi.status()!=WL_CONNECTED&&wifiNetworks.connecting()){ui.notify("Wi-Fi connecting...");return;}
 SyncFiles::recover();if(!syncMode.enter(SyncTransport::WiFi))return;
 usingSta=WiFi.status()==WL_CONNECTED;
 if(running){server.stop();MDNS.end();running=false;} // Re-arm with an explicit session.
 if(!usingSta){wifiNetworks.suspendForSync();WiFi.mode(WIFI_AP);if(!WiFi.softAP(Config::WIFI_AP_SSID,Config::WIFI_AP_PASS)){syncMode.leave(SyncTransport::WiFi);wifiNetworks.resumeAfterSync();ui.notify("Wi-Fi AP failed");return;}}
 if(!routesReady){routes();routesReady=true;}wifiOta.newSession();server.begin();running=true;syncModeActive=true;autoSession=false;waitingForConsent=false;stopRequested=false;
 MONOPOD_LOG.printf("[Wi-Fi Sync] %s HTTP at http://%s:80\n",usingSta?"LAN":"AP",ip().c_str());ui.redraw();
}
void WiFiSyncService::end(){
 if(!syncModeActive||busy())return;
 wifiOta.stopSession();server.stop();if(usingSta)MDNS.end();running=false;syncModeActive=false;stopRequested=false;
 const bool wasAuto=autoSession;autoSession=false;sessionToken="";requestApproved=false;
 if(!usingSta){WiFi.softAPdisconnect(true);WiFi.mode(WIFI_OFF);}
 media.rescan(true);syncMode.leave(SyncTransport::WiFi);
 if(!usingSta)wifiNetworks.resumeAfterSync();
 ui.begin();
 if(wasAuto && WiFi.status()==WL_CONNECTED)startListener();
}
void WiFiSyncService::loop(){
 if(!syncModeActive){
   if(WiFi.status()==WL_CONNECTED)startListener();
   else if(running)stopListener();
 }
 if(!running)return;
 if(usingSta)wifiNetworks.tick();
 server.handleClient();
 if(waitingForConsent && millis()-requestSince>30000UL){waitingForConsent=false;requestDenied=true;ui.redraw();}
 if(syncModeActive){
   SyncFiles::tick();wifiOta.loop();
   if(uploadActive&&!SyncFiles::busy()){uploadActive=false;uploadOk=false;}
   if(stopRequested)end();
 }
}
#endif
