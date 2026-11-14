#pragma once
#include "Arduino.h"
struct Preferences {
  bool begin(const char*,bool readOnly){return readOnly||ClockNative::writable;}
  void end(){}
  String getString(const char* key,const char* fallback){auto i=ClockNative::strings.find(key);return i==ClockNative::strings.end()?fallback:i->second;}
  bool getBool(const char* key,bool fallback){if(std::string(key)=="24hour")return ClockNative::hour24; if(std::string(key)=="auto-ntp")return ClockNative::persistedNtp;return fallback;}
  size_t putString(const char* key,const String& value){if(!ClockNative::writable)return 0;ClockNative::strings[key]=value;return value.size();}
  size_t putBool(const char* key,bool value){if(!ClockNative::writable)return 0;if(std::string(key)=="24hour")ClockNative::hour24=value;else ClockNative::persistedNtp=value;return 1;}
};
