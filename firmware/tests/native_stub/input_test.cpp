#include "Input.h"
#include <cassert>
#include <cstdio>
uint32_t fakeTime=0;
int fakeGPIO[40];
static Key tick(uint32_t ms){fakeTime=ms;return input.poll();}
int main(){
  for(int &pin : fakeGPIO)pin=HIGH;
  input.begin();
  assert(tick(10)==Key::None);
  fakeGPIO[32]=LOW;
  assert(tick(100)==Key::None); // start debounce
  assert(tick(140)==Key::None); // short Select must not fire on press
  fakeGPIO[32]=HIGH;
  assert(tick(250)==Key::None);
  assert(tick(290)==Key::Select); // fires on release only
  fakeGPIO[32]=LOW;
  assert(tick(1000)==Key::None);
  assert(tick(1040)==Key::None);
  assert(tick(2539)==Key::None);
  assert(tick(2540)==Key::SelectLong);
  assert(tick(2700)==Key::None);
  fakeGPIO[32]=HIGH;
  assert(tick(2710)==Key::None);
  assert(tick(2750)==Key::None); // release cannot also click
  fakeGPIO[13]=LOW;
  assert(tick(2800)==Key::None);
  assert(tick(2840)==Key::Previous);
  printf("PASS native input: short select, long select, no duplicate, previous\n");
}
