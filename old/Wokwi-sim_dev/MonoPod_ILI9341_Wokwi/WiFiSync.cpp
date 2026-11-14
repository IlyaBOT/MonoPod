#include "WiFiSync.h"
#include "Library.h"
#include "AudioEngine.h"
#include <SD.h>
#include <WiFi.h>
#include "ClockService.h"
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
  server.onNotFound([this](){server.send(404,"text/plain","Not found");});
  server.begin();running=true;
}
void WiFiSyncService::end(){
  if(!running)return;
  server.stop();if(uploadFile)uploadFile.close();
  WiFi.softAPdisconnect(true);WiFi.mode(WIFI_OFF);running=false;
}
void WiFiSyncService::loop(){if(running)server.handleClient();}
#endif
