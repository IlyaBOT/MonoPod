#include "WiFiSync.h"
#include "UsbSync.h"
#include "Library.h"
#include "AudioEngine.h"
#include <SD.h>
#include <WiFi.h>
#include "ClockService.h"
#include "Display.h"
#include "Theme.h"
#include "UI.h"
#include "Settings.h"
#include "SyncMode.h"
#include "WiFiNetworks.h"
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
    "<input type='file' name='track' accept='.mp3,.wav,.flac,.aac'>"
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
    uploadActive=true;
    uploadSuccess=false;uploadError="";uploadName="";
    if(!syncMode.canWrite(SyncTransport::WiFi) || !media.sdOK()){
      uploadError="Not in Wi-Fi sync or no SD";return;
    }
    uploadName=safeName(u.filename);
    if(!MediaLibrary::isAudio(uploadName) || uploadName.endsWith(".m4a")){
      uploadError="Unsupported file";return;
    }
    if(!SD.exists("/Music") && !SD.mkdir("/Music")){
      uploadError="Cannot create Music directory";return;
    }
    SD.remove("/Music/.monopod-wifi.part");
    uploadFile=SD.open("/Music/.monopod-wifi.part",FILE_WRITE);
    if(!uploadFile)uploadError="Cannot open temporary file";
  }else if(u.status==UPLOAD_FILE_WRITE){
    if(uploadFile && uploadFile.write(u.buf,u.currentSize)!=u.currentSize){
      uploadError="Write failed";uploadFile.close();
    }
  }else if(u.status==UPLOAD_FILE_END){
    if(uploadFile){uploadFile.flush();uploadFile.close();uploadSuccess=uploadError.isEmpty();}
  }else if(u.status==UPLOAD_FILE_ABORTED){
    if(uploadFile)uploadFile.close();uploadError="Aborted";
    SD.remove("/Music/.monopod-wifi.part");
    uploadActive=false;
  }
}
void WiFiSyncService::finished(){
  static const char* part="/Music/.monopod-wifi.part";
  static const char* backup="/Music/.monopod-wifi.bak";
  static const char* journal="/Music/.monopod-wifi.target";
  if(uploadFile)uploadFile.close();
  bool ok=uploadSuccess && uploadError.isEmpty() && !uploadName.isEmpty();
  if(ok){
    String dest="/Music/"+uploadName;
    if(SD.exists(backup)||SD.exists(journal)){
      ok=false;uploadError="Old backup pending";
    }
    const bool hadOld=SD.exists(dest.c_str());
    if(ok && hadOld){
      File j=SD.open(journal,FILE_WRITE);
      if(!j){ok=false;uploadError="Cannot write recovery journal";}
      else{j.println(uploadName);j.flush();j.close();}
    }
    if(ok && hadOld && !SD.rename(dest.c_str(),backup)){
      SD.remove(journal);ok=false;uploadError="Cannot back up old track";
    }
    if(ok && !SD.rename(part,dest.c_str())){
      if(hadOld)SD.rename(backup,dest.c_str());
      ok=false;uploadError="Cannot commit uploaded track";
    }
    if(ok && hadOld){SD.remove(backup);SD.remove(journal);}
    if(!ok && hadOld && SD.exists(backup) && !SD.exists(dest.c_str()))
      SD.rename(backup,dest.c_str());
    if(!ok && !SD.exists(backup))SD.remove(journal);
  }
  if(!ok)SD.remove(part);
  uploadActive=false;
  if(ok){media.rescan();ui.redraw();server.send(200,"text/plain","Track saved or replaced; library updated.");}
  else server.send(400,"text/plain",uploadError.isEmpty()?"No file written":uploadError);
  uploadName="";
}
void WiFiSyncService::themeUpload(){
  HTTPUpload &u=server.upload();
  if(u.status==UPLOAD_FILE_START){
    themeUploadActive=true;
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
    themeUploadActive=false;
  }
}
void WiFiSyncService::themeFinished(){
  themeUploadActive=false;
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
  if(running || usbSync.busy() || syncMode.active())return;
  if(!media.sdOK()){ui.notify("No microSD card");return;}
  // Recover an interrupted rename before opening the network writer.
  const char* journal="/Music/.monopod-wifi.target";
  const char* backup="/Music/.monopod-wifi.bak";
  if(SD.exists(journal)){
    File j=SD.open(journal,FILE_READ);
    String name=j?j.readStringUntil('\n'):String();
    if(j)j.close();name.trim();
    if(!name.isEmpty() && safeName(name)==name && MediaLibrary::isAudio(name)){
      String dest="/Music/"+name;
      if(SD.exists(backup)){
        if(SD.exists(dest.c_str()))SD.remove(backup);
        else SD.rename(backup,dest.c_str());
      }
      if(!SD.exists(backup))SD.remove(journal);
    }
  }
  SD.remove("/Music/.monopod-wifi.part");
  // The moment the user explicitly starts Wi-Fi Sync, audio and UI are locked.
  if(!syncMode.enter(SyncTransport::WiFi))return;
  wifiNetworks.leave();  // stop STA scans before switching radio to AP mode
  WiFi.mode(WIFI_AP);
  if(!WiFi.softAP(Config::WIFI_AP_SSID,Config::WIFI_AP_PASS)){
    WiFi.mode(WIFI_OFF);syncMode.leave(SyncTransport::WiFi);
    ui.notify("Wi-Fi AP failed");return;
  }
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
    String body="{\"firmware\":\"MonoPod v0.8.5\",\"tracks\":"+String((unsigned)media.size())+
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
  if(!running || busy())return; // never close files or remove SD during HTTP transfer
  server.stop();if(uploadFile)uploadFile.close();if(themeUploadFile)themeUploadFile.close();
  WiFi.softAPdisconnect(true);WiFi.mode(WIFI_OFF);running=false;
  syncMode.leave(SyncTransport::WiFi);
}
void WiFiSyncService::loop(){if(running)server.handleClient();}
#endif
