// Match Arduino.h's macros before compiling the real header, then run the
// existing interpolation tests. The wheel algorithm itself is not changed.
#define PI 3.1415926535897932384626433832795
#define TWO_PI 6.283185307179586476925286766559
#include "WheelDetents.h"
#undef PI
#undef TWO_PI
#include "wheel_detents_native_test.cpp"
