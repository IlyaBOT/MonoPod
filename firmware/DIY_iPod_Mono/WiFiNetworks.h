#pragma once
#include "Config.h"
#include <Arduino.h>
#include <vector>
struct AccessPoint {String ssid; int32_t rssi=0;bool secured=false;};
class WiFiNetworkManager {
public:
  void enter();
  void leave();
  void tick();
  void connect(const String& ssid,const String& password);
  bool scanning() const{return scanRunning;}
  bool connecting()const{return connectRunning;}
  bool connected()const;
  bool changed(){bool b=dirty;dirty=false;return b;}
  const std::vector<AccessPoint>& networks()const{return aps;}
  String status()const;
  String connectedSSID()const;
  void disconnect();
private:
  std::vector<AccessPoint> aps;
  bool active=false,scanRunning=false,connectRunning=false,dirty=false;
  bool hasScan=false;
  uint32_t lastScan=0,connectionStart=0;
  String message="";
  String target, candidatePassword;
  void startScan();
};
extern WiFiNetworkManager wifiNetworks;
