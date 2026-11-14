#pragma once
#include "Config.h"
#if FEATURE_WIFI_SYNC
#include <WebServer.h>
#include <SD.h>
class WiFiSyncService {
public:
  void begin();
  void end();
  void loop();
  bool active() const {return running;}
  String ip() const;
private:
  WebServer server{80};
  File uploadFile;
  File themeUploadFile;
  bool themeUploadSuccess=false;
  String themeUploadError;
  size_t themeUploadSize=0;
  bool running=false, uploadSuccess=false;
  String uploadError;
  void page();
  void upload();
  void finished();
  void themeUpload();
  void themeFinished();
};
#else
class WiFiSyncService {
public:
  void begin(){}void end(){}void loop(){}bool active() const{return false;}
  String ip()const{return "Disabled";}
};
#endif
extern WiFiSyncService wifiSync;
