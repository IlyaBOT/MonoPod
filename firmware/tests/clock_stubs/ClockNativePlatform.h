#pragma once
#include "Arduino.h"
// Host adapters never change the Windows clock, TZ, network or NVS.
struct timeval {time_t tv_sec;long tv_usec;};
inline int settimeofday(const timeval* value,void*){ClockNative::epoch=value->tv_sec;return 0;}
inline int clock_native_setenv(const char*,const char* value,int){ClockNative::timezone=value;return 0;}
inline void clock_native_tzset(){}
inline time_t clock_native_time(time_t* value){if(value)*value=ClockNative::epoch;return ClockNative::epoch;}
inline tm* clock_native_localtime(const time_t* value,tm* result){
#ifdef _WIN32
  return gmtime_s(result,value)==0?result:nullptr;
#else
  return gmtime_r(value,result);
#endif
}
#define setenv clock_native_setenv
#define tzset clock_native_tzset
#define time clock_native_time
#define localtime_r clock_native_localtime
