#include "ClockService.h"
#include "ClockConfigValidation.h"
#include <WiFi.h>
#include <Preferences.h>
#include <sys/time.h>
#include <time.h>
#include <esp_sntp.h>
#include <ctype.h>
ClockService clockService;
namespace {
constexpr time_t MIN_VALID_TIME=1600000000;
bool validTz(const String& value){return ClockConfigValidation::timezone(value.c_str());}
}
void ClockService::begin(){
  Preferences p;
  if(p.begin("monopod-clock",true)){
    String tz=p.getString("tz",Config::TIMEZONE_TZ);
    if(validTz(tz))tz_=tz;
    ntp_=p.getBool("auto-ntp",false);
    hour24_=p.getBool("24hour",true);
    String server=p.getString("ntp-server","pool.ntp.org");
    if(ClockConfigValidation::ntpServer(server.c_str()))ntpServer_=server;
    p.end();
  }
  setenv("TZ",tz_.c_str(),1);tzset();
}
void ClockService::setUnix(time_t epoch){
  if(epoch<MIN_VALID_TIME || epoch>4102444800LL)return;
  timeval t={epoch,0};settimeofday(&t,nullptr);
}
bool ClockService::setTimezone(const String& value){
  if(!validTz(value))return false;
  Preferences p;if(!p.begin("monopod-clock",false))return false;
  size_t saved=p.putString("tz",value);p.end();if(!saved)return false;
  const bool changed=tz_!=value;
  tz_=value;setenv("TZ",tz_.c_str(),1);tzset();
  if(changed)restartNtp();
  return true;
}
bool ClockService::set24Hour(bool on){
  Preferences p;if(!p.begin("monopod-clock",false))return false;
  size_t n=p.putBool("24hour",on);p.end();if(n!=1)return false;
  hour24_=on;return true;
}
bool ClockService::setNtpEnabled(bool enabled){
  Preferences p;if(!p.begin("monopod-clock",false))return false;
  size_t saved=p.putBool("auto-ntp",enabled);p.end();if(saved!=1)return false;
  ntp_=enabled;
  if(!enabled)restartNtp();
  return true;
}
void ClockService::restartNtp(){
  if(ntpStarted_ && esp_sntp_enabled())esp_sntp_stop();
  ntpStarted_=false;
}
bool ClockService::setNtpServer(const String& value){
  if(!ClockConfigValidation::ntpServer(value.c_str()))return false;
  Preferences p;if(!p.begin("monopod-clock",false))return false;
  size_t saved=p.putString("ntp-server",value);p.end();if(!saved)return false;
  const bool changed=ntpServer_!=value;
  // Stop SNTP before replacing the storage passed to configTzTime().
  if(changed)restartNtp();
  ntpServer_=value;return true;
}
void ClockService::loop(){
  // No blocking DNS / SNTP calls in the UI loop. LWIP services NTP on its own.
  // Do not trigger when operating as an isolated Wi-Fi Sync access point.
  const bool online=WiFi.status()==WL_CONNECTED;
  if(!online){
    if(ntpStarted_ && esp_sntp_enabled())esp_sntp_stop();
    ntpStarted_=false;return;
  }
  if(ntp_ && !ntpStarted_){
    configTzTime(tz_.c_str(),ntpServer_.c_str(),ntpServer_=="pool.ntp.org"?"time.google.com":nullptr);
    ntpStarted_=true;
    MONOPOD_LOG.println("Clock: started SNTP in STA mode");
  }
}
bool ClockService::valid() const{return time(nullptr)>MIN_VALID_TIME;}
String ClockService::dateTime() const{
  if(!valid())return "Time not set";
  time_t t=time(nullptr);tm value;localtime_r(&t,&value);
  char b[32];strftime(b,sizeof(b),hour24_?"%d.%m.%Y  %H:%M":"%d.%m.%Y  %I:%M %p",&value);return String(b);
}
String ClockService::shortTime() const{
  if(!valid())return "";
  time_t t=time(nullptr);tm value;localtime_r(&t,&value);
  char b[24];strftime(b,sizeof(b),hour24_?"%H:%M":"%I:%M %p",&value);
  return !hour24_ && b[0]=='0'?String(b+1):String(b);
}
