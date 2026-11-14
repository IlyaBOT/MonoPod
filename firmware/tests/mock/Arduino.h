#pragma once
#include <cstdint>
#include <algorithm>
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define CONFIG_IDF_TARGET_ESP32S3 1
#define constrain(x,a,b) std::clamp((x),(a),(b))
struct MockSerial { void println(const char*) {} };
extern MockSerial Serial0;
extern uint32_t mockMs;
extern int mockOutput, mockPwmDuty, mockPin;
extern bool mockPwmActive;
extern int mockAttachCount, mockDetachCount;
inline uint32_t millis(){return mockMs;}
inline void pinMode(int pin,int mode){(void)pin;(void)mode;}
inline void digitalWrite(int pin,int high){mockPin=pin;mockOutput=high;}
inline bool ledcAttach(uint8_t pin,uint32_t hz,uint8_t bits){(void)pin;(void)hz;(void)bits;mockAttachCount++;mockPwmActive=true;return true;}
inline bool ledcWrite(uint8_t pin,uint32_t duty){(void)pin;mockPwmDuty=duty;return true;}
inline bool ledcDetach(uint8_t pin){(void)pin;mockDetachCount++;mockPwmActive=false;return true;}
