#include <cassert>
#include <cstdio>
#include "Backlight.h"
#include "Config.h"
MockSerial Serial0;
uint32_t mockMs=0;
int mockOutput=-1,mockPwmDuty=-1,mockPin=-1;
bool mockPwmActive=false;
int mockAttachCount=0,mockDetachCount=0;
static void advance(BacklightController &b,uint32_t finish){
  while(mockMs<finish){mockMs+=10;b.loop();}
}
int main(){
 static_assert(!Config::TFT_BL_BRIGHTNESS_CONTROL,"fixed mode enabled by default");
 BacklightController b;
 b.begin(35);
 assert(b.enabled() && mockOutput==LOW && !mockPwmActive);
 b.wake();advance(b,300);
 assert(b.lit() && mockOutput==HIGH && !mockPwmActive);
 assert(mockAttachCount>0 && mockDetachCount>0);
 b.setBrightness(5);assert(mockOutput==HIGH && !mockPwmActive);
 b.sleep();advance(b,750);
 assert(!b.lit() && mockOutput==LOW && !mockPwmActive);
 b.wake();advance(b,1100);
 assert(mockOutput==HIGH && !mockPwmActive);
 std::puts("PASS fixed backlight: steady HIGH/LOW, PWM only during fades");
}
