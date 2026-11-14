// Compile the real Config.h with a minimal host Arduino type shim. The child
// process changes only its own TZ environment, never the PC or device clock.
#define CONFIG_IDF_TARGET_ESP32S3 1
#define ARDUINO_USB_CDC_ON_BOOT 1
#include "Config.h"
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <iostream>

int main(){
  const time_t epoch=1793548200; // 2026-11-01 15:50:00 UTC == 18:50 MSK
#ifdef _WIN32
  if(_putenv_s("TZ",Config::TIMEZONE_TZ)!=0)return 1;
  _tzset();
  tm local{};if(localtime_s(&local,&epoch)!=0)return 2;
#else
  if(setenv("TZ",Config::TIMEZONE_TZ,1)!=0)return 1;
  tzset();
  tm local{};if(!localtime_r(&epoch,&local))return 2;
#endif
  if(std::strcmp(Config::TIMEZONE_TZ,"UTC-3")!=0 || local.tm_year!=126 || local.tm_mon!=10 ||
     local.tm_mday!=1 || local.tm_hour!=18 || local.tm_min!=50)return 3;
  std::cout<<"PASS real Config.h Moscow default: 15:50 UTC -> 18:50 UTC+03:00 via host C runtime; no clock writes\n";
  return 0;
}
