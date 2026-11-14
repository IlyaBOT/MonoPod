#include "WiFiNetworks.h"
#include "WiFiSync.h"
#include <WiFi.h>
#include <Preferences.h>
#include <algorithm>
WiFiNetworkManager wifiNetworks;
void WiFiNetworkManager::startScan(){
  if(!active||scanRunning||connectRunning)return;
  WiFi.scanDelete();
  int ret=WiFi.scanNetworks(true,true,false,280);
  if(ret==WIFI_SCAN_RUNNING){scanRunning=true;message="Scanning...";dirty=true;}
  else if(ret>=0){scanRunning=false;lastScan=millis();}
  else {message="Scan failed";dirty=true;lastScan=millis();}
}
void WiFiNetworkManager::enter(){
  if(active)return;
#if FEATURE_WIFI_SYNC
  if(wifiSync.active())wifiSync.end();
#endif
  WiFi.mode(WIFI_STA);active=true;scanRunning=false;connectRunning=false;hasScan=false;
  dirty=true;message="Scanning...";startScan();
  // Auto-connect last paired network, if credentials exist.
  Preferences pref;pref.begin("monopod-wifi",true);
  String ssid=pref.getString("ssid","");String pw=pref.getString("pass","");pref.end();
  if(ssid.length())connect(ssid,pw);
}
void WiFiNetworkManager::leave(){
  active=false;
  if(scanRunning){WiFi.scanDelete();scanRunning=false;}
  // Keep STA connection alive for time sync or future sync features.
}
void WiFiNetworkManager::disconnect(){
  WiFi.disconnect();connectRunning=false;message="Disconnected";dirty=true;
  Preferences p;p.begin("monopod-wifi",false);p.remove("ssid");p.remove("pass");p.end();
}
void WiFiNetworkManager::connect(const String& ssid,const String& pass){
  if(ssid.isEmpty())return;
  if(scanRunning){WiFi.scanDelete();scanRunning=false;}
  target=ssid;candidatePassword=pass;WiFi.begin(ssid.c_str(),pass.c_str());connectRunning=true;
  connectionStart=millis();message="Connecting...";dirty=true;

}
bool WiFiNetworkManager::connected()const{return WiFi.status()==WL_CONNECTED;}
String WiFiNetworkManager::connectedSSID()const{return connected()?WiFi.SSID():"";}
String WiFiNetworkManager::status()const{return connected()?String("Connected: ")+WiFi.SSID():message;}
void WiFiNetworkManager::tick(){
  if(!active)return;
  if(connectRunning){
    if(connected()) {
      connectRunning=false;message="Connected";dirty=true;
      Preferences p;p.begin("monopod-wifi",false);
      p.putString("ssid",target);p.putString("pass",candidatePassword);p.end();
      candidatePassword="";
    }
    else if(millis()-connectionStart>16000){connectRunning=false;message="Connection failed";dirty=true;}
    return;
  }
  if(scanRunning){
    int ret=WiFi.scanComplete();
    if(ret==WIFI_SCAN_RUNNING)return;
    scanRunning=false;lastScan=millis();hasScan=true;
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
    aps.swap(next);WiFi.scanDelete();message=aps.empty()?"No networks":"";dirty=true;
  }else if(millis()-lastScan>7000){startScan();}
}
