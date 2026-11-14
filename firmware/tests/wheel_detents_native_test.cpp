#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "../DIY_iPod_Mono/WheelDetents.h"
static void expect(bool ok,const char* text){if(!ok){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}
int main(){
  WheelDetents w;
  w.anchor(0);
  // Very slow motion: 100 tiny steps must not get discarded by a deadband.
  int ticks=0;
  for(int i=1;i<=100;i++)ticks+=w.sample((float)i*1.04719755f/100.0f);
  for(int i=0;i<8;i++)ticks+=w.sample(1.04719755f); // let IIR converge
  expect(ticks==1,"one electrode sector at very slow speed produces exactly one step");
  // A full slow circle should advance about six one-step detents.
  w.reset();w.anchor(0);ticks=0;
  for(int i=1;i<=600;i++)ticks+=w.sample((float)i*6.28318530718f/600.0f);
  for(int i=0;i<10;i++)ticks+=w.sample(0);
  expect(ticks>=6 && ticks<=7,"full circle stays near six steps without skipping sectors");
  // Backwards movement and angular wrap remain well-defined.
  w.reset();w.anchor(-3.0f);ticks=0;
  for(int i=1;i<=100;i++)ticks+=w.sample(-3.0f-(float)i*1.05f/100.0f);
  for(int i=0;i<10;i++)ticks+=w.sample(-4.05f);
  expect(ticks==-1,"negative wrap produces one backwards step");
  // Random near-stationary jitter cannot move selection.
  w.reset();w.anchor(1.5f);ticks=0;
  for(int i=0;i<600;i++)ticks+=w.sample(1.5f+0.025f*sinf(i*0.71f));
  expect(ticks==0,"stationary jitter does not scroll");
  std::puts("WheelDetents: 4 native tests passed");
}
