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
#include <WiFi.h>
WiFiSyncService wifiSync;
#if FEATURE_WIFI_SYNC
namespace {
bool number(const String& s,uint32_t& value,int base=10){if(s.isEmpty()||s.length()>10)return false;for(size_t i=0;i<s.length();i++)if(base==10?!isdigit((unsigned char)s[i]):!isxdigit((unsigned char)s[i]))return false;unsigned long long v=strtoull(s.c_str(),nullptr,base);if(v>0xffffffffULL)return false;value=v;return true;}
}
String WiFiSyncService::ip()const{return running?(usingSta?WiFi.localIP():WiFi.softAPIP()).toString():"Offline";}
bool WiFiSyncService::busy()const{return uploadActive||SyncFiles::busy();}
bool WiFiSyncService::allowed(){if(!SyncFiles::gate(SyncTransport::WiFi)){server.send(409,"text/plain",SyncFiles::error);return false;}return true;}
void WiFiSyncService::result(bool ok){if(ok)server.send(200,"application/json","{\"ok\":true}");else{String e=SyncFiles::error;int code=e=="NOT_FOUND"?404:e=="SD_WRITE"||e=="SD_READ"?500:e=="NO_SPACE"?507:409;server.send(code,"text/plain",e);}}
void WiFiSyncService::upload(){
 HTTPUpload& u=server.upload();
 if(u.status==UPLOAD_FILE_START){
  if(uploadActive){uploadError="Only one multipart file is allowed";SyncFiles::abort();return;}
  uploadActive=true;uploadOk=false;uploadError="";
  if(!SyncFiles::gate(SyncTransport::WiFi)){uploadError=SyncFiles::error;return;}
  bool legacy=server.uri()!="/api/fs/upload",theme=server.uri()=="/api/theme"||server.arg("theme")=="1";
  String path=theme?"/MonoPod/theme.mpt":legacy?"/Music/"+u.filename:server.arg("path");uint32_t size=0,crc=0;
  if(!legacy && (!number(server.arg("size"),size)||server.arg("crc").length()!=8||!number(server.arg("crc"),crc,16))){uploadError="INVALID_ARGUMENTS";return;}
  if(legacy&&!theme){if(u.filename.indexOf('/')>=0||u.filename.indexOf('\\')>=0||!MediaLibrary::isAudio(u.filename)){uploadError="INVALID_FILE";return;}if(!SD.exists("/Music"))SD.mkdir("/Music");}
  if(!SyncFiles::begin(path,size,crc,legacy||server.arg("replace")=="1",theme,SyncTransport::WiFi,legacy))uploadError=SyncFiles::error;
 }else if(u.status==UPLOAD_FILE_WRITE){
  if(uploadError.length())return;if(!SyncFiles::write(u.buf,u.currentSize)){uploadError=SyncFiles::error;SyncFiles::abort();}
 }else if(u.status==UPLOAD_FILE_END){uploadOk=uploadError.isEmpty()&&SyncFiles::busy();}
 else if(u.status==UPLOAD_FILE_ABORTED){SyncFiles::abort();uploadError="ABORTED";uploadActive=false;uploadOk=false;}
}
void WiFiSyncService::finished(){
 bool ok=uploadOk&&uploadError.isEmpty();if(ok&&!SyncFiles::commit()){uploadError=SyncFiles::error;ok=false;}
 if(!ok)SyncFiles::abort();uploadActive=false;uploadOk=false;
 if(ok){if(server.uri()=="/upload")media.rescan();server.send(200,"application/json","{\"ok\":true}");ui.redraw();}else server.send(422,"text/plain",uploadError.length()?uploadError:"NO_UPLOAD");
}
void WiFiSyncService::routes(){
 server.on("/",HTTP_GET,[this](){server.send(200,"text/html; charset=utf-8","<!doctype html><title>MonoPod Sync</title><h2>MonoPod Wi-Fi Sync</h2><p>Local HTTP. Use MonoPod Desktop for the file manager.</p><form action='/upload' method='post' enctype='multipart/form-data'><input type='file' name='track' accept='.mp3,.wav,.flac,.aac'><button>Upload music</button></form>");});
 server.on("/api/info",HTTP_GET,[this](){server.send(200,"application/json",SyncFiles::info());});
 server.on("/api/storage",HTTP_GET,[this](){if(allowed())server.send(200,"application/json",SyncFiles::info());});
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
 server.on("/upload",HTTP_POST,[this](){finished();},[this](){upload();});
 server.on("/api/theme",HTTP_POST,[this](){finished();},[this](){upload();});
 server.on("/api/smoothing",HTTP_POST,[this](){if(!allowed())return;if(!server.hasArg("enabled")){server.send(400,"text/plain","Missing enabled");return;}settings.data.fontSmoothing=server.arg("enabled")=="1";settings.markDirty();result(true);});
 server.on("/api/theme/reset",HTTP_POST,[this](){if(!allowed())return;if(busy()){server.send(409,"text/plain","TRANSFER_ACTIVE");return;}if(SD.exists("/MonoPod/theme.mpt")&&!SD.remove("/MonoPod/theme.mpt")){server.send(500,"text/plain","DELETE_FAILED");return;}theme.unload();display.palette(settings.data.contrast);result(true);});
 server.on("/api/scan",HTTP_POST,[this](){if(!allowed())return;if(busy()){server.send(409,"text/plain","TRANSFER_ACTIVE");return;}media.rescan();result(true);});
 server.on("/api/sync/end",HTTP_POST,[this](){if(!allowed())return;if(busy()){server.send(409,"text/plain","TRANSFER_ACTIVE");return;}server.send(200,"application/json","{\"ok\":true}");stopRequested=true;});
 server.on("/time",HTTP_GET,[this](){if(!allowed())return;time_t epoch=(time_t)strtoll(server.arg("unix").c_str(),nullptr,10);if(epoch<1600000000||epoch>4102444800LL){server.send(400,"text/plain","Invalid time");return;}clockService.setUnix(epoch);result(true);});
 server.onNotFound([this](){server.send(404,"text/plain","Not found");});
}
void WiFiSyncService::begin(){
 if(running||usbSync.busy()||syncMode.active())return;if(!media.sdOK()){ui.notify("No microSD card");return;}
 if(WiFi.status()!=WL_CONNECTED&&wifiNetworks.connecting()){ui.notify("Wi-Fi connecting...");return;}
 SyncFiles::recover();if(!syncMode.enter(SyncTransport::WiFi))return;
 usingSta=WiFi.status()==WL_CONNECTED;
 if(!usingSta){wifiNetworks.suspendForSync();WiFi.mode(WIFI_AP);if(!WiFi.softAP(Config::WIFI_AP_SSID,Config::WIFI_AP_PASS)){syncMode.leave(SyncTransport::WiFi);wifiNetworks.resumeAfterSync();ui.notify("Wi-Fi AP failed");return;}}
 if(!routesReady){routes();routesReady=true;}server.begin();running=true;stopRequested=false;
 MONOPOD_LOG.printf("[Wi-Fi Sync] %s HTTP at http://%s:80\n",usingSta?"LAN":"AP",ip().c_str());ui.redraw();
}
void WiFiSyncService::end(){if(!running||busy())return;server.stop();if(!usingSta){WiFi.softAPdisconnect(true);WiFi.mode(WIFI_OFF);}running=false;stopRequested=false;media.rescan();syncMode.leave(SyncTransport::WiFi);if(!usingSta)wifiNetworks.resumeAfterSync();ui.begin();}
void WiFiSyncService::loop(){if(running){if(usingSta)wifiNetworks.tick();server.handleClient();SyncFiles::tick();if(uploadActive&&!SyncFiles::busy()){uploadActive=false;uploadOk=false;}if(stopRequested)end();}}
#endif
