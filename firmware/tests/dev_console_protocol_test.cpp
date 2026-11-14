#include "../DIY_iPod_Mono/DevConsoleProtocol.h"
#include <cassert>
#include <cstdio>
using namespace DevConsoleProtocol;
int main(){
  Command c=parse("$DISABLE_HARDWARE_CONTROL=1");
  assert(c.type==Type::SetHardwareDisabled && c.disableHardware);
  c=parse("$DISABLE_HARDWARE_CONTROL=0");
  assert(c.type==Type::SetHardwareDisabled && !c.disableHardware);
  c=parse("$MENU");assert(c.type==Type::Key && c.button==Button::Menu);
  c=parse("$KEY=SELECT_LONG");assert(c.type==Type::Key && c.button==Button::SelectLong);
  c=parse("$PLAY");assert(c.type==Type::Key && c.button==Button::PlayPause);
  c=parse("$SCROLL=UP,5");assert(c.type==Type::Scroll && c.button==Button::Up && c.count==5);
  c=parse("$SCROLL=DOWN,32");assert(c.type==Type::Scroll && c.button==Button::Down && c.count==32);
  assert(parse("$SCROLL=DOWN,0").type==Type::Invalid);
  assert(parse("$SCROLL=DOWN,33").type==Type::Invalid);
  assert(parse("$SCROLL=DOWN,-1").type==Type::Invalid);
  assert(parse("$SCROLL=SIDEWAYS,3").type==Type::Invalid);
  assert(parse("$DISABLE_HARDWARE_CONTROL=yes").type==Type::Invalid);
  assert(parse("$UNKNOWN").type==Type::Invalid);
  assert(parse("$STATUS").type==Type::Status);
  assert(parse("$BATTERY").type==Type::Battery);
  assert(parse("$SD").type==Type::Sd);
  assert(parse("$HELP").type==Type::Help);
  puts("PASS console parser: hardware toggle, keys, bounded scroll, status, invalid commands");
}
