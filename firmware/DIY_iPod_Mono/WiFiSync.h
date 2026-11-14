#pragma once
#include "Config.h"
#if FEATURE_WIFI_SYNC
#include <WebServer.h>
#include "WiFiOta.h"
class WiFiSyncService {
public:void begin();void end();void loop();bool active()const{return running;}bool busy()const;String ip()const;String otaLine()const{return wifiOta.displayLine();}bool otaUpdating()const{return wifiOta.inProgress();}
private:WebServer server{80};bool routesReady=false,running=false,stopRequested=false,usingSta=false,uploadActive=false,uploadOk=false,otaUploadSeen=false,otaUploadOk=false;String uploadError;void upload();void finished();void otaUpload();void otaFinished();void result(bool ok);bool allowed();void routes();
};
#else
class WiFiSyncService {public:void begin(){}void end(){}void loop(){}bool active()const{return false;}bool busy()const{return false;}String ip()const{return "Disabled";}String otaLine()const{return "OTA disabled";}bool otaUpdating()const{return false;}};
#endif
extern WiFiSyncService wifiSync;
