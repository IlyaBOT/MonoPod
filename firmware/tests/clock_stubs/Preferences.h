#pragma once
#include "Arduino.h"
struct Preferences {
  bool begin(const char*,bool readOnly){return readOnly||ClockNative::writable;}
  void end(){}
  String getString(const char* key,const char* fallback){auto i=ClockNative::strings.find(key);return i==ClockNative::strings.end()?fallback:i->second;}
  bool getBool(const char*,bool){return ClockNative::persistedNtp;}
  size_t putString(const char* key,const String& value){if(!ClockNative::writable)return 0;ClockNative::strings[key]=value;return value.size();}
  size_t putBool(const char*,bool value){if(!ClockNative::writable)return 0;ClockNative::persistedNtp=value;return 1;}
};
