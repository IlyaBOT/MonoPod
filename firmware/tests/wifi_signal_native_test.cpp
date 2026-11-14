#include <cassert>
#include <cstdint>
#include "../DIY_iPod_Mono/WiFiSignal.h"
int main(){
 using namespace WiFiSignal;
 static_assert(barsForRssi(-45)==3);
 static_assert(barsForRssi(-55)==3);
 static_assert(barsForRssi(-56)==2);
 static_assert(barsForRssi(-70)==2);
 static_assert(barsForRssi(-71)==1);
 static_assert(barsForRssi(-95)==1);
 static_assert(!refreshDue(11999,0));
 static_assert(refreshDue(12000,0));
 static_assert(refreshDue(50,0xfffffff0u,64u));
 static_assert(!refreshDue(49,0xfffffff0u,66u));
 return 0;
}
