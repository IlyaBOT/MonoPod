#pragma once
#include "Config.h"
#include "WiFiSignal.h"
#include <Arduino.h>
#include <vector>
struct AccessPoint {String ssid; int32_t rssi=0;bool secured=false;};
class WiFiNetworkManager {
public:
  void begin();          // Boot-time restore from NVS, independent of the Wi-Fi menu.
  void enter();          // Scan UI only; never auto-connect on menu entry.
  void leave();
  void tick();           // Always call in NORMAL mode, including off Wi-Fi pages.
  void connect(const String& ssid,const String& password);
  void disconnect();     // User action: erase stored credentials and stop retries.
  void suspendForSync(); // Wi-Fi Sync owns the radio in AP mode.
  void resumeAfterSync();
  bool scanning() const{return scanRunning;}
  bool connecting()const{return connectRunning;}
  // Credentials were loaded at boot from NVS, or saved on a successful connection.
  bool hasSavedNetwork() const { return !savedSSID.isEmpty(); }
  bool connected()const;
  bool changed(){bool b=dirty;dirty=false;return b;}
  uint8_t signalBars() const{return connected()?cachedBars:0;}
  uint8_t indicator() const; // 0=hidden, 1-3=bars, 4/5=connecting blink phases
  const std::vector<AccessPoint>& networks()const{return aps;}
  String status()const;
  String connectedSSID()const;
private:
  std::vector<AccessPoint> aps;
  bool active=false,scanRunning=false,connectRunning=false,dirty=false;
  bool suspended=false,pendingSave=false,wasConnected=false;
  uint8_t cachedBars=0;
  uint32_t lastScan=0,connectionStart=0,nextRetryAt=0,lastRssiSample=0;
  String message="";
  String target,candidatePassword,savedSSID,savedPassword;
  void startScan();
  void beginConnection(const String& ssid,const String& password,bool saveWhenConnected);
  void saveCredentials();
};
extern WiFiNetworkManager wifiNetworks;
