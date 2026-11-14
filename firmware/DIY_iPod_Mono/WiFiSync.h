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
  bool busy() const {return uploadActive || themeUploadActive;}
  String ip() const;
private:
  WebServer server{80};
  File uploadFile;
  File themeUploadFile;
  bool themeUploadSuccess=false;
  String themeUploadError;
  size_t themeUploadSize=0;
  bool apMode=false; // true only when no working STA association exists
  bool running=false, uploadSuccess=false;
  bool uploadActive=false,themeUploadActive=false;
  String uploadError,uploadName;
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
  bool busy() const{return false;}
  String ip()const{return "Disabled";}
};
#endif
extern WiFiSyncService wifiSync;
