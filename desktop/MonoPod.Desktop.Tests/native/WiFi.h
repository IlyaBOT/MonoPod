#pragma once
#include "Arduino.h"
#define WL_CONNECTED 3
struct IP {String toString(){return "192.168.1.10";}};struct Wifi {int status(){return WL_CONNECTED;}IP localIP(){return {};}IP softAPIP(){return {};}};extern Wifi WiFi;
