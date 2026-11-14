#pragma once
#include "Config.h"
#if FEATURE_WIFI_SYNC
#include <WebServer.h>
#include "WiFiOta.h"
class WiFiSyncService {
public:void begin();void end();void loop();bool active()const{return syncModeActive;}bool busy()const;String ip()const;String otaLine()const{return wifiOta.displayLine();}bool otaUpdating()const{return wifiOta.inProgress();}
  bool pending()const{return waitingForConsent;}String requester()const{return requestName;}
  void answer(bool yes);
private:WebServer server{80};bool routesReady=false,running=false,syncModeActive=false,autoSession=false,stopRequested=false,usingSta=false,uploadActive=false,uploadOk=false,otaUploadSeen=false,otaUploadOk=false,waitingForConsent=false,requestApproved=false,requestDenied=false;
  bool wifiPowerOverride=false;
  uint8_t wifiPreviousPowerSave=0;
  void boostTransferWiFi();void restoreTransferWiFi();
  uint32_t requestSince=0;IPAddress requestIP;String requestName="MonoPod Desktop",sessionToken,uploadError;
  void startListener();void stopListener();bool sessionAuthorized();void upload();void finished();void otaUpload();void otaFinished();void result(bool ok);bool allowed();void routes();
};
#else
class WiFiSyncService {public:void begin(){}void end(){}void loop(){}bool active()const{return false;}bool busy()const{return false;}String ip()const{return "Disabled";}String otaLine()const{return "OTA disabled";}bool otaUpdating()const{return false;}};
#endif
extern WiFiSyncService wifiSync;
