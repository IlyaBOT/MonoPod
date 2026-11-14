#include "../DIY_iPod_Mono/BacklightMath.h"
#include <cassert>
int main(){
  using namespace BacklightMath;
  assert(ease(0,250)==0);
  assert(ease(250,250)==1024);
  assert(ease(1000,250)==1024);
  assert(ease(125,250)>=510 && ease(125,250)<=514);
  assert(gammaDuty(0)==0);
  assert(gammaDuty(100)==1023);
  uint16_t prev=0;
  for(int i=0;i<=100;i++){auto duty=gammaDuty(i);assert(duty>=prev);prev=duty;}
  uint16_t last=0;
  for(int i=0;i<=250;i++){
    const auto e=ease(i,250);assert(e>=last);last=e;
    assert(mix(0,75,e)<=75);
    assert(mix(75,0,e)<=75);
  }
  assert(mix(0,75,0)==0);
  assert(mix(0,75,1024)==75);
  assert(mix(75,0,1024)==0);
}
