#include "ClockService.h"
#include <time.h>
#include <sys/time.h>
#if FEATURE_DS3231
#include <Wire.h>
static uint8_t bcd(uint8_t b){return (b&0xf)+10*(b>>4);}
#endif
ClockService clockService;
void ClockService::setUnix(time_t seconds){timeval t={seconds,0};settimeofday(&t,nullptr);}
void ClockService::begin(){
  setenv("TZ",Config::TIMEZONE_TZ,1);tzset();
#if FEATURE_DS3231
  // DS3231 requires two extra I2C pins. See README; disabled by default.
  Wire.begin(Config::PIN_HAPTIC_SDA,Config::PIN_HAPTIC_SCL); // board-dependent I2C pins
  Wire.beginTransmission(0x68);Wire.write((uint8_t)0);if(Wire.endTransmission(false)!=0)return;
  if(Wire.requestFrom(0x68,7)!=7)return;
  int second=bcd(Wire.read()&0x7f),minute=bcd(Wire.read()&0x7f),hour=bcd(Wire.read()&0x3f);
  Wire.read();int day=bcd(Wire.read()&0x3f),month=bcd(Wire.read()&0x1f),year=bcd(Wire.read())+100;
  tm value={};value.tm_year=year;value.tm_mon=month-1;value.tm_mday=day;
  value.tm_hour=hour;value.tm_min=minute;value.tm_sec=second;
  time_t t=mktime(&value);if(t>1600000000)setUnix(t);
#endif
}
bool ClockService::valid() const{return time(nullptr)>1600000000;}
String ClockService::dateTime() const{
  if(!valid())return "Time not set";
  time_t t=time(nullptr);tm tmp;localtime_r(&t,&tmp);
  char b[32];strftime(b,sizeof b,"%d.%m.%Y  %H:%M",&tmp);return String(b);
}
