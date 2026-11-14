#pragma once
#include "Arduino.h"
constexpr int WL_CONNECTED=3;
struct NativeWiFi {int status()const{return ClockNative::online?WL_CONNECTED:0;}};
inline NativeWiFi WiFi;
