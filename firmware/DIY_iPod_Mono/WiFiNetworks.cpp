#include "WiFiNetworks.h"
#include "WiFiSync.h"
#include "SyncMode.h"
#include <WiFi.h>
#include <Preferences.h>
#include <algorithm>
WiFiNetworkManager wifiNetworks;
namespace {
constexpr uint32_t kConnectTimeoutMs=16000;
constexpr uint32_t kRetryDelayMs=15000;
constexpr uint32_t kRssiIntervalMs=12000;
constexpr uint32_t kBlinkIntervalMs=650;
}
void WiFiNetworkManager::begin(){
  Preferences p;
  if(p.begin("monopod-wifi",true)){
    savedSSID=p.getString("ssid","");
    savedPassword=p.getString("pass","");
    p.end();
  }
  if(!savedSSID.isEmpty()){
    MONOPOD_LOG.println("Wi-Fi: restoring saved network from NVS");
    beginConnection(savedSSID,savedPassword,false);
  }
}
void WiFiNetworkManager::saveCredentials(){
  Preferences p;
  if(!p.begin("monopod-wifi",false)){
    MONOPOD_LOG.println("Wi-Fi: NVS open failed; credentials not saved");
    return;
  }
  const size_t n=p.putString("ssid",target);
  const size_t m=p.putString("pass",candidatePassword);
  p.end();
  if(!n || (!candidatePassword.isEmpty() && !m)){
    MONOPOD_LOG.println("Wi-Fi: NVS write failed");
    return;
  }
  savedSSID=target;savedPassword=candidatePassword;
  MONOPOD_LOG.println("Wi-Fi: credentials saved to NVS");
}
void WiFiNetworkManager::beginConnection(const String& ssid,const String& pass,bool saveWhenConnected){
  if(ssid.isEmpty()||suspended)return;
  if(scanRunning){WiFi.scanDelete();scanRunning=false;}
  target=ssid;candidatePassword=pass;pendingSave=saveWhenConnected;
  WiFi.mode(WIFI_STA);
  // Own the bounded retry cadence here, not in a synchronous wait or driver loop.
  WiFi.setAutoReconnect(false);
  WiFi.begin(ssid.c_str(),pass.c_str());
  connectRunning=true;wasConnected=false;cachedBars=0;
  connectionStart=millis();message="Connecting...";dirty=true;
}
void WiFiNetworkManager::connect(const String& ssid,const String& password){
  if(ssid.isEmpty() || ssid.length()>32 || password.length()>63)return;
  beginConnection(ssid,password,true);
}
void WiFiNetworkManager::startScan(){
  if(!active||scanRunning||connectRunning||suspended)return;
  WiFi.mode(WIFI_STA);
  WiFi.scanDelete();
  int ret=WiFi.scanNetworks(true,true,false,280);
  if(ret==WIFI_SCAN_RUNNING){scanRunning=true;message="Scanning...";dirty=true;}
  else if(ret>=0){scanRunning=true;lastScan=millis();} // read result on next tick
  else {message="Scan failed";dirty=true;lastScan=millis();}
}
void WiFiNetworkManager::enter(){
  if(active)return;
  active=true;lastScan=millis()-7001UL;dirty=true;
  // Saved network may already be connecting, so never initiate a second join.
  if(!connectRunning)startScan();
}
void WiFiNetworkManager::leave(){
  active=false;
  if(scanRunning){WiFi.scanDelete();scanRunning=false;}
}
void WiFiNetworkManager::suspendForSync(){
  leave();suspended=true;
  connectRunning=false;wasConnected=false;cachedBars=0;pendingSave=false;
  dirty=true;
}
void WiFiNetworkManager::resumeAfterSync(){
  suspended=false;
  message="Disconnected";dirty=true;
  // Rejoin previously paired STA once AP-only Sync releases the radio.
  if(!savedSSID.isEmpty())beginConnection(savedSSID,savedPassword,false);
}
void WiFiNetworkManager::disconnect(){
  leave();
  savedSSID="";savedPassword="";target="";candidatePassword="";
  connectRunning=false;wasConnected=false;cachedBars=0;pendingSave=false;nextRetryAt=0;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Preferences p;
  if(p.begin("monopod-wifi",false)){
    p.remove("ssid");p.remove("pass");p.end();
  }else MONOPOD_LOG.println("Wi-Fi: failed to open NVS for erase");
  message="Disconnected";dirty=true;
}
bool WiFiNetworkManager::connected()const{return !suspended && WiFi.status()==WL_CONNECTED;}
String WiFiNetworkManager::connectedSSID()const{return connected()?WiFi.SSID():"";}
String WiFiNetworkManager::status()const{return connected()?String("Connected: ")+WiFi.SSID():message;}
uint8_t WiFiNetworkManager::indicator() const {
  if(connected())return cachedBars;
  if(connectRunning)return (millis()/kBlinkIntervalMs)%2U ? 4 : 5;
  return 0;
}
void WiFiNetworkManager::tick(){
  // The radio belongs exclusively to Wi-Fi Sync in AP mode.
  if(suspended || syncMode.active())return;
  const uint32_t now=millis();
  const bool online=connected();
  if(online){
    if(!wasConnected || connectRunning){
      connectRunning=false;
      if(pendingSave)saveCredentials();
      pendingSave=false;candidatePassword="";message="Connected";dirty=true;
      lastRssiSample=now-kRssiIntervalMs;
    }
    wasConnected=true;
    if(WiFiSignal::refreshDue(now,lastRssiSample,kRssiIntervalMs)){
      lastRssiSample=now;
      const uint8_t next=WiFiSignal::barsForRssi(WiFi.RSSI());
      if(cachedBars!=next){cachedBars=next;dirty=true;}
    }
  }else{
    if(wasConnected){
      wasConnected=false;cachedBars=0;
      message="Disconnected";dirty=true;
      nextRetryAt=now+kRetryDelayMs;
    }
    if(connectRunning && (uint32_t)(now-connectionStart)>=kConnectTimeoutMs){
      WiFi.disconnect(false);
      connectRunning=false;pendingSave=false;candidatePassword="";
      message="Connection failed";dirty=true;nextRetryAt=now+kRetryDelayMs;
    }
    // Do not retry when manually disconnected, in password entry or while scanning.
    if(!connectRunning && !scanRunning && !savedSSID.isEmpty() &&
       (int32_t)(now-nextRetryAt)>=0){
      nextRetryAt=now+kRetryDelayMs;
      beginConnection(savedSSID,savedPassword,false);
    }
  }
  if(!active)return;
  if(scanRunning){
    int ret=WiFi.scanComplete();
    if(ret==WIFI_SCAN_RUNNING)return;
    scanRunning=false;lastScan=now;
    if(ret<0){message="Scan failed";dirty=true;WiFi.scanDelete();return;}
    std::vector<AccessPoint> next;
    for(int i=0;i<ret;i++){
      String name=WiFi.SSID(i);if(name.isEmpty())continue;
      bool seen=false;for(const auto& n:next)if(n.ssid==name){seen=true;break;}
      if(seen)continue;
      AccessPoint a;a.ssid=name;a.rssi=WiFi.RSSI(i);
      a.secured=(WiFi.encryptionType(i)!=WIFI_AUTH_OPEN);
      next.push_back(a);
    }
    std::sort(next.begin(),next.end(),[](const AccessPoint&a,const AccessPoint&b){return a.rssi>b.rssi;});
    if(next.size()>24)next.resize(24);
    aps.swap(next);WiFi.scanDelete();dirty=true;
  }else if(!connectRunning && (uint32_t)(now-lastScan)>7000UL){startScan();}
}
