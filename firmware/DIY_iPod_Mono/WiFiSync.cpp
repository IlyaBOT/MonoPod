#include "WiFiSync.h"
#include "Library.h"
#include "AudioEngine.h"
#include <SD.h>
#include <WiFi.h>
#include "ClockService.h"
#include "Display.h"
#include "Theme.h"
#include "UI.h"
#include "Settings.h"
WiFiSyncService wifiSync;
#if FEATURE_WIFI_SYNC
String WiFiSyncService::ip() const {return running?WiFi.softAPIP().toString():"Offline";}
void WiFiSyncService::page(){
  server.send(200,"text/html; charset=utf-8",
    "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>MonoPod Sync</title><style>body{max-width:560px;margin:2em auto;padding:1em;"
    "font:16px system-ui}button,input{margin:8px 0;padding:8px}</style>"
    "<h2>MonoPod Sync</h2><p>Upload MP3, WAV, FLAC or AAC to /Music.</p>"
    "<form action='/upload' method='post' enctype='multipart/form-data'>"
    "<input type='file' name='track' multiple accept='.mp3,.wav,.flac,.aac'>"
    "<button type='submit'>Upload</button></form>"
    "<button onclick=\"fetch('/time?unix='+Math.floor(Date.now()/1000)).then(()=>alert('Time set'))\">Set clock from this device</button>"
    "<p>Local AP, HTTP only. No Internet required.</p>");
}
static String safeName(String s){
  int cut=max(s.lastIndexOf('/'),s.lastIndexOf('\\'));if(cut>=0)s=s.substring(cut+1);
  String out;
  for(unsigned int i=0;i<s.length()&&out.length()<90;i++){
    const unsigned char c=(unsigned char)s[i];
    if(c<32||c==127||c=='/'||c=='\\'||c==':'||c=='<'||c=='>'||c=='"'||c=='|')continue;
    out+=(char)c;
  }
  if(out.startsWith("."))out="track"+out;
  return out;
}
void WiFiSyncService::upload(){
  HTTPUpload &u=server.upload();
  if(u.status==UPLOAD_FILE_START){
    player.stop();uploadSuccess=false;uploadError="";
    String file=safeName(u.filename);
    if(!MediaLibrary::isAudio(file) || file.endsWith(".m4a")){
      uploadError="Unsupported file";return;
    }
    if(!SD.exists("/Music"))SD.mkdir("/Music");
    String path="/Music/"+file;
    if(SD.exists(path.c_str())){uploadError="File already exists";return;}
    uploadFile=SD.open(path.c_str(),FILE_WRITE);
    if(!uploadFile)uploadError="Cannot open file";
  }else if(u.status==UPLOAD_FILE_WRITE){
    if(uploadFile && uploadFile.write(u.buf,u.currentSize)!=u.currentSize){
      uploadError="Write failed";uploadFile.close();
    }
  }else if(u.status==UPLOAD_FILE_END){
    if(uploadFile){uploadFile.flush();uploadFile.close();uploadSuccess=uploadError.isEmpty();}
  }else if(u.status==UPLOAD_FILE_ABORTED){
    if(uploadFile)uploadFile.close();uploadError="Aborted";
  }
}
void WiFiSyncService::finished(){
  if(uploadSuccess){media.rescan();server.send(200,"text/plain","Upload complete. Library rescanned.");}
  else server.send(400,"text/plain",uploadError.isEmpty()?"No file written":uploadError);
}
void WiFiSyncService::themeUpload(){
  HTTPUpload &u=server.upload();
  if(u.status==UPLOAD_FILE_START){
    themeUploadSuccess=false;themeUploadError="";themeUploadSize=0;
    if(themeUploadFile)themeUploadFile.close();
    if(!media.sdOK()){themeUploadError="No SD";return;}
    if(!SD.exists("/MonoPod"))SD.mkdir("/MonoPod");
    SD.remove("/MonoPod/theme.tmp");
    themeUploadFile=SD.open("/MonoPod/theme.tmp",FILE_WRITE);
    if(!themeUploadFile)themeUploadError="Cannot write theme";
  }else if(u.status==UPLOAD_FILE_WRITE){
    if(themeUploadError.length())return;
    themeUploadSize+=u.currentSize;
    if(themeUploadSize>ThemeManager::MAX_BYTES+28){themeUploadError="Theme is too large";themeUploadFile.close();return;}
    if(!themeUploadFile||themeUploadFile.write(u.buf,u.currentSize)!=u.currentSize){
      themeUploadError="SD write failed";if(themeUploadFile)themeUploadFile.close();
    }
  }else if(u.status==UPLOAD_FILE_END){
    if(themeUploadFile){themeUploadFile.flush();themeUploadFile.close();}
    themeUploadSuccess=themeUploadError.isEmpty();
  }else if(u.status==UPLOAD_FILE_ABORTED){
    if(themeUploadFile)themeUploadFile.close();
    SD.remove("/MonoPod/theme.tmp");themeUploadError="Aborted";
  }
}
void WiFiSyncService::themeFinished(){
  if(!themeUploadSuccess){SD.remove("/MonoPod/theme.tmp");server.send(400,"text/plain",themeUploadError.isEmpty()?"No theme data":themeUploadError);return;}
  // Parse + CRC-verify before replacing an existing theme.
  static ThemeManager probe;
  if(!probe.reload("/MonoPod/theme.tmp")){
    SD.remove("/MonoPod/theme.tmp");server.send(422,"text/plain","Invalid MPT1 theme or CRC");return;
  }
  probe.unload();
  const bool hadOld=SD.exists("/MonoPod/theme.mpt");
  SD.remove("/MonoPod/theme.bak");
  if(hadOld && !SD.rename("/MonoPod/theme.mpt","/MonoPod/theme.bak")){
    server.send(500,"text/plain","Cannot back up old theme");return;
  }
  if(!SD.rename("/MonoPod/theme.tmp","/MonoPod/theme.mpt")){
    if(hadOld)SD.rename("/MonoPod/theme.bak","/MonoPod/theme.mpt");
    server.send(500,"text/plain","Cannot install new theme");return;
  }
  if(!display.reloadTheme()){
    SD.remove("/MonoPod/theme.mpt");
    if(hadOld)SD.rename("/MonoPod/theme.bak","/MonoPod/theme.mpt");
    display.reloadTheme();
    server.send(500,"text/plain","Failed to activate theme");return;
  }
  SD.remove("/MonoPod/theme.bak");ui.redraw();
  server.send(200,"application/json","{\"ok\":true,\"theme\":\"installed\"}");
}
void WiFiSyncService::begin(){
  if(running||!media.sdOK())return;
  WiFi.mode(WIFI_AP);
  if(!WiFi.softAP(Config::WIFI_AP_SSID,Config::WIFI_AP_PASS)){WiFi.mode(WIFI_OFF);return;}
  server.on("/",HTTP_GET,[this](){page();});
  server.on("/time",HTTP_GET,[this](){
    if(!server.hasArg("unix")){server.send(400,"text/plain","Missing unix");return;}
    time_t epoch=(time_t)strtoll(server.arg("unix").c_str(),nullptr,10);
    if(epoch<1600000000 || epoch>4102444800LL){server.send(400,"text/plain","Invalid time");return;}
    clockService.setUnix(epoch);server.send(200,"text/plain","Clock updated");
  });
  server.on("/upload",HTTP_POST,[this](){finished();},[this](){upload();});
  server.on("/api/theme",HTTP_POST,[this](){themeFinished();},[this](){themeUpload();});
  server.on("/api/info",HTTP_GET,[this](){
    String body="{\"firmware\":\"MonoPod v0.6\",\"tracks\":"+String((unsigned)media.size())+
      ",\"sd_ready\":"+String(media.sdOK()?"true":"false")+
      ",\"sd_total_mb\":"+String((uint32_t)(media.totalBytes()/1024/1024))+
      ",\"sd_used_mb\":"+String((uint32_t)(media.usedBytes()/1024/1024))+
      ",\"font_smoothing\":"+String(settings.data.fontSmoothing?"true":"false")+
      ",\"custom_theme\":"+String(display.themeLoaded()?"true":"false")+"}";
    server.send(200,"application/json",body);
  });
  server.on("/api/smoothing",HTTP_POST,[this](){
    if(!server.hasArg("enabled")){server.send(400,"text/plain","Missing enabled");return;}
    settings.data.fontSmoothing=(server.arg("enabled")=="1");
    settings.markDirty();ui.redraw();
    server.send(200,"application/json",String("{\"font_smoothing\":")+
      (settings.data.fontSmoothing?"true":"false")+"}");
  });
  server.on("/api/theme/reset",HTTP_POST,[this](){
    if(!media.sdOK()){server.send(503,"text/plain","SD unavailable");return;}
    SD.remove("/MonoPod/theme.mpt");
    theme.unload();display.palette(settings.data.contrast);ui.redraw();
    server.send(200,"application/json","{\"ok\":true}");
  });
  server.onNotFound([this](){server.send(404,"text/plain","Not found");});
  server.begin();running=true;
}
void WiFiSyncService::end(){
  if(!running)return;
  server.stop();if(uploadFile)uploadFile.close();if(themeUploadFile)themeUploadFile.close();
  WiFi.softAPdisconnect(true);WiFi.mode(WIFI_OFF);running=false;
}
void WiFiSyncService::loop(){if(running)server.handleClient();}
#endif
