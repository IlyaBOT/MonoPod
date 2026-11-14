#pragma once
#include <string>
#include <cstdint>
#include <ctime>
#include <map>
using String=std::string;
namespace ClockNative {
inline std::map<std::string,std::string> strings;
inline bool persistedNtp=false, writable=true, online=false, sntp=false;
inline unsigned starts=0,stops=0;
inline std::string server1,server2,timezone;
inline time_t epoch=0;
inline void reset(){strings.clear();persistedNtp=false;writable=true;online=false;sntp=false;starts=stops=0;server1.clear();server2.clear();timezone.clear();epoch=0;}
}
struct NativeLog {void println(const char*){}};
inline NativeLog nativeLog;
#define MONOPOD_LOG nativeLog
inline void configTzTime(const char* tz,const char* first,const char* second=nullptr){
  ClockNative::timezone=tz;ClockNative::server1=first;ClockNative::server2=second?second:"";
  ClockNative::sntp=true;++ClockNative::starts;
}
