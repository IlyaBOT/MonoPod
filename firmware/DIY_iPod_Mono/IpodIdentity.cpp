#include "IpodIdentity.h"
#include <inttypes.h>
#if defined(ARDUINO_ARCH_ESP32) && !WOKWI_SIMULATION
#include <Preferences.h>
#include <esp_mac.h>
#include <esp_system.h>
#endif
namespace {
  char serial[17] = "0000000000000000";
  bool validEfuse = false;
  bool initialized = false;
}
void IpodIdentity::begin() {
  if(initialized)return;
  initialized=true;
  uint64_t id=0;
#if defined(ARDUINO_ARCH_ESP32) && !WOKWI_SIMULATION
  uint8_t mac[6]{};
  if(esp_efuse_mac_get_default(mac)==ESP_OK){
    bool empty=true;for(uint8_t b:mac)if(b){empty=false;break;}
    if(!empty){
      validEfuse=true;
      id=0x4D50000000000000ULL;
      for(uint8_t i=0;i<6;i++)id |= uint64_t(mac[i]) << (8*(5-i));
    }
  }
  if(!validEfuse){
    Preferences prefs;
    if(prefs.begin("monopod-id",false)){
      id=prefs.getULong64("guid64",0);
      if(!id){
        id=0x4D50000000000000ULL |
           ((uint64_t(esp_random())<<32)|esp_random())&0x0000FFFFFFFFFFFFULL;
        prefs.putULong64("guid64",id);
      }
      prefs.end();
    }
  }
#else
  id=0x4D50000000000001ULL; // deterministic simulation marker
#endif
#if defined(ARDUINO_ARCH_ESP32) && !WOKWI_SIMULATION
  if(!id){ // NVS unavailable: still create an ID, but it may change after reboot.
    id=0x4D50000000000000ULL |
       (((uint64_t(esp_random())<<32)|esp_random())&0x0000FFFFFFFFFFFFULL);
  }
#endif
  // No false "Apple genuine serial" claim: this is our own stable GUID.
  snprintf(serial,sizeof(serial),"%016llX",(unsigned long long)id);
}
const char* IpodIdentity::guid(){if(!initialized)begin();return serial;}
bool IpodIdentity::fromEfuse(){if(!initialized)begin();return validEfuse;}
