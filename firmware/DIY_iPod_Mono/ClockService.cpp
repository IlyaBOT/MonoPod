#include "ClockService.h"
#include <WiFi.h>
#include <Preferences.h>
#include <sys/time.h>
#include <time.h>
#include <esp_sntp.h>
#include <ctype.h>
ClockService clockService;
namespace {
constexpr time_t MIN_VALID_TIME=1600000000;
bool validTz(const String& value){
  if(value.isEmpty() || value.length()>63)return false;
  for(size_t i=0;i<value.length();i++){
    const char c=value[i];
    // POSIX TZ: prevent NVS control characters and malformed quoted input.
    if(!(isalnum((unsigned char)c) || c=='+' || c=='-' || c==':' ||
         c==',' || c=='.' || c=='<' || c=='>' || c=='/'))return false;
  }
  return true;
}
}
void ClockService::begin(){
  Preferences p;
  if(p.begin("monopod-clock",true)){
    String tz=p.getString("tz",Config::TIMEZONE_TZ);
    if(validTz(tz))tz_=tz;
    ntp_=p.getBool("auto-ntp",false);
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
  tz_=value;setenv("TZ",tz_.c_str(),1);tzset();
  Preferences p;if(!p.begin("monopod-clock",false))return false;
  size_t saved=p.putString("tz",tz_);p.end();return saved>0;
}
bool ClockService::setNtpEnabled(bool enabled){
  ntp_=enabled;
  if(!enabled){
    if(ntpStarted_ && esp_sntp_enabled())esp_sntp_stop();
    ntpStarted_=false;
  }
  Preferences p;if(!p.begin("monopod-clock",false))return false;
  size_t saved=p.putBool("auto-ntp",enabled);p.end();return saved==1;
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
    configTzTime(tz_.c_str(),"pool.ntp.org","time.google.com");
    ntpStarted_=true;
    MONOPOD_LOG.println("Clock: started SNTP in STA mode");
  }
}
bool ClockService::valid() const{return time(nullptr)>MIN_VALID_TIME;}
String ClockService::dateTime() const{
  if(!valid())return "Time not set";
  time_t t=time(nullptr);tm value;localtime_r(&t,&value);
  char b[32];strftime(b,sizeof(b),"%d.%m.%Y  %H:%M",&value);return String(b);
}
String ClockService::shortTime() const{
  if(!valid())return "";
  time_t t=time(nullptr);tm value;localtime_r(&t,&value);
  char b[24];strftime(b,sizeof(b),"%I:%M %p",&value);
  return b[0]=='0'?String(b+1):String(b);
}
