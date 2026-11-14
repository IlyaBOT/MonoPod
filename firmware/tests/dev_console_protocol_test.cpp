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
  assert(parse("$CLICKER_STATUS").type==Type::ClickerStatus);
  Command mute=parse("$CLICKER_MUTE=1");
  assert(mute.type==Type::ClickerMute && mute.muteClicker);
  mute=parse("$CLICKER_MUTE=0");
  assert(mute.type==Type::ClickerMute && !mute.muteClicker);
  assert(parse("$CLICKER_MUTE=2").type==Type::Invalid);
  assert(parse("$SD_RETRY").type==Type::Invalid);
  assert(parse("$ADC_DIAG").type==Type::Invalid);
  assert(parse("$WIFI_STATUS").type==Type::WifiStatus);
  mute=parse("$CLICKER=1");assert(mute.type==Type::ClickerMute && !mute.muteClicker);
  mute=parse("$CLICKER=0");assert(mute.type==Type::ClickerMute && mute.muteClicker);
  puts("PASS console parser: hardware toggle, keys, bounded scroll, status, invalid commands");
}
// v0.8.8: clicker isolation and SD re-probe commands.
// Keep the assertions in the existing test main above, not here.
