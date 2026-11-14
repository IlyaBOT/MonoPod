#pragma once
#include "Arduino.h"
inline bool esp_sntp_enabled(){return ClockNative::sntp;}
inline void esp_sntp_stop(){if(ClockNative::onStop)ClockNative::onStop();ClockNative::sntp=false;++ClockNative::stops;}
