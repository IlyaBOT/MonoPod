#pragma once
// ASCII-only, allocation-free parser shared by device and native regression tests.
// Diagnostic UART0 must never be mixed with binary MPOD1 USB CDC transport.
#include <stdint.h>
#include <string.h>

namespace DevConsoleProtocol {
enum class Type : uint8_t { Invalid, SetHardwareDisabled, Key, Scroll, Help, Status, Battery, Sd, SdRetry, ClickerMute, ClickerStatus, AdcDiag, WifiStatus };
enum class Button : uint8_t { None, Up, Down, Menu, Select, SelectLong, Previous, Next, PlayPause };
struct Command {
  Type type=Type::Invalid;
  Button button=Button::None;
  uint8_t count=0;
  bool disableHardware=false;
  bool muteClicker=false;
};
inline Button buttonOf(const char* s) {
  if(!strcmp(s,"UP"))return Button::Up;
  if(!strcmp(s,"DOWN"))return Button::Down;
  if(!strcmp(s,"MENU"))return Button::Menu;
  if(!strcmp(s,"SELECT"))return Button::Select;
  if(!strcmp(s,"SELECT_LONG"))return Button::SelectLong;
  if(!strcmp(s,"PREVIOUS")||!strcmp(s,"PREV"))return Button::Previous;
  if(!strcmp(s,"NEXT"))return Button::Next;
  if(!strcmp(s,"PLAY_PAUSE")||!strcmp(s,"PLAY"))return Button::PlayPause;
  return Button::None;
}
inline bool parseCount(const char* s,uint8_t& count) {
  if(!*s)return false;
  unsigned v=0;
  while(*s){if(*s<'0'||*s>'9')return false;v=v*10u+(unsigned)(*s++-'0');if(v>32)return false;}
  if(v==0)return false;
  count=(uint8_t)v;return true;
}
inline Command parse(const char* line) {
  Command c;
  if(!line||line[0]!='$')return c;
  const char* body=line+1;
  if(!strcmp(body,"DISABLE_HARDWARE_CONTROL=1")||!strcmp(body,"DISABLE_HARDWARE_CONTROL=0")){
    c.type=Type::SetHardwareDisabled;
    c.disableHardware=body[strlen(body)-1]=='1';return c;
  }
  if(!strcmp(body,"HELP")){c.type=Type::Help;return c;}
  if(!strcmp(body,"STATUS")){c.type=Type::Status;return c;}
  if(!strcmp(body,"BATTERY")){c.type=Type::Battery;return c;}
  if(!strcmp(body,"ADC_DIAG")){c.type=Type::AdcDiag;return c;}
  if(!strcmp(body,"WIFI_STATUS")){c.type=Type::WifiStatus;return c;}
  if(!strcmp(body,"SD")){c.type=Type::Sd;return c;}
  if(!strcmp(body,"SD_RETRY")){c.type=Type::SdRetry;return c;}
  if(!strcmp(body,"CLICKER_STATUS")){c.type=Type::ClickerStatus;return c;}
  if(!strcmp(body,"CLICKER=1")||!strcmp(body,"CLICKER=0")){
    c.type=Type::ClickerMute;c.muteClicker=body[strlen(body)-1]=='0';return c;
  }
  if(!strcmp(body,"CLICKER_MUTE=1")||!strcmp(body,"CLICKER_MUTE=0")){
    c.type=Type::ClickerMute;c.muteClicker=body[strlen(body)-1]=='1';return c;
  }
  if(!strncmp(body,"KEY=",4))body+=4;
  // $SCROLL=UP,5 and $SCROLL=DOWN,5 are bounded, non-blocking key sequences.
  if(!strncmp(body,"SCROLL=",7)){
    const char* dir=body+7;
    const char* comma=strchr(dir,',');
    if(!comma)return c;
    const size_t n=(size_t)(comma-dir);
    if(n==2 && !strncmp(dir,"UP",2))c.button=Button::Up;
    else if(n==4 && !strncmp(dir,"DOWN",4))c.button=Button::Down;
    else return Command{};
    if(!parseCount(comma+1,c.count))return Command{};
    c.type=Type::Scroll;return c;
  }
  c.button=buttonOf(body);
  if(c.button!=Button::None){c.type=Type::Key;c.count=1;}
  return c;
}
}
