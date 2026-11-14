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
  bool use24Hour() const { return hour24_; }
  bool set24Hour(bool on);
  void setUnix(time_t epoch);
  bool setTimezone(const String& posix);
  bool setNtpEnabled(bool enabled);
  bool setNtpServer(const String& server);
  bool ntpEnabled() const {return ntp_;}
  String timezone() const {return tz_;}
  String ntpServer() const {return ntpServer_;}
private:
  String tz_=Config::TIMEZONE_TZ;
  bool ntp_=false;
  bool hour24_=true;
  bool ntpStarted_=false;
  String ntpServer_="pool.ntp.org";
  void restartNtp();
};
extern ClockService clockService;
