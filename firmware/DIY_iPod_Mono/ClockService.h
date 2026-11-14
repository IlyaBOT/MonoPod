#pragma once
#include "Config.h"
#include <Arduino.h>
#include <time.h>
// No RTC chip in this prototype. Time must be restored from Desktop or NTP
// after each power cycle. TZ string is POSIX style and persisted in NVS.
class ClockService {
public:
  void begin();
  void loop();
  bool valid() const;
  String dateTime() const;
  String shortTime() const;
  void setUnix(time_t epoch);
  bool setTimezone(const String& posix);
  bool setNtpEnabled(bool enabled);
  bool ntpEnabled() const {return ntp_;}
  String timezone() const {return tz_;}
private:
  String tz_=Config::TIMEZONE_TZ;
  bool ntp_=false;
  bool ntpStarted_=false;
};
extern ClockService clockService;
