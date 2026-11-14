#pragma once
#include <string>
#include <cstdint>
#include <ctime>
#include <map>
#include <functional>
using String=std::string;
namespace ClockNative {
inline std::map<std::string,std::string> strings;
inline bool persistedNtp=false, hour24=true, writable=true, online=false, sntp=false;
inline unsigned starts=0,stops=0;
inline std::string server1,server2,timezone;
inline time_t epoch=0;
inline std::function<void()> onStop;
inline void reset(){strings.clear();persistedNtp=false;hour24=true;writable=true;online=false;sntp=false;starts=stops=0;server1.clear();server2.clear();timezone.clear();epoch=0;onStop={};}
}
struct NativeLog {void println(const char*){}};
inline NativeLog nativeLog;
#define MONOPOD_LOG nativeLog
inline void configTzTime(const char* tz,const char* first,const char* second=nullptr){
  ClockNative::timezone=tz;ClockNative::server1=first;ClockNative::server2=second?second:"";
  ClockNative::sntp=true;++ClockNative::starts;
}
