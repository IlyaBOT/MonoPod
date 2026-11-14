#pragma once
#include "Config.h"
#if FEATURE_WIFI_SYNC
#include <WebServer.h>
class WiFiSyncService {
public:void begin();void end();void loop();bool active()const{return running;}bool busy()const;String ip()const;
private:WebServer server{80};bool routesReady=false,running=false,stopRequested=false,usingSta=false,uploadActive=false,uploadOk=false;String uploadError;void upload();void finished();void result(bool ok);bool allowed();void routes();
};
#else
class WiFiSyncService {public:void begin(){}void end(){}void loop(){}bool active()const{return false;}bool busy()const{return false;}String ip()const{return "Disabled";}};
#endif
extern WiFiSyncService wifiSync;
