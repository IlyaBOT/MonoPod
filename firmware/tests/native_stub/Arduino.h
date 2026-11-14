#pragma once
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
using String = std::string;
using uint8_t = std::uint8_t;
using uint16_t = std::uint16_t;
using uint32_t = std::uint32_t;
#define INPUT_PULLUP 2
#define INPUT 0
#define LOW 0
#define HIGH 1
#define ADC_11db 3
extern uint32_t fakeTime;
extern int fakeGPIO[40];
inline uint32_t millis(){return fakeTime;}
inline void delay(int ms){fakeTime+=ms;}
inline void pinMode(int,int){}
inline int digitalRead(int pin){return fakeGPIO[pin];}
inline void analogReadResolution(int){}
inline void analogSetPinAttenuation(int,int){}
inline int analogReadMilliVolts(int){return 3300;}
inline int touchRead(int){return 100;}
template <typename T> T min(T a,T b){return a<b?a:b;}
template <typename T> T max(T a,T b){return a>b?a:b;}
