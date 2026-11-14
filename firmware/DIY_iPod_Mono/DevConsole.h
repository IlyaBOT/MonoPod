#pragma once
#include <Arduino.h>
#include "Types.h"

class DevelopmentConsole {
public:
  void begin();
  void loop();
  Key pollKey();
  bool hardwareDisabled() const {return disabled;}
  void physicalKey(Key key); // only hardware event, never simulated CLI key
private:
  static constexpr size_t CONSOLE_LINE_CAPACITY = 95;
  char line[CONSOLE_LINE_CAPACITY+1]={};
  uint8_t length=0;
  bool overflow=false;
  bool disabled=false;  // runtime-only; resets to false after reboot
  bool clickerMuteRequested=false;
  bool buttonsDebug=false;
  bool touchDebug=false;
  uint32_t lastTouchLog=0;
  uint32_t lastButtonsLog=0;
  int lastButtonsCode=-1;
  Key queued=Key::None;
  uint8_t repeats=0;
  uint32_t lastKeyAt=0;
  bool keySequenceStarted=false;
  void handle(const char* command);
  void printStatus();
  void printButtonsSample(bool force);
  void printTouchSample(bool force);
  void printTouchSettings();
  void printHelp();
};
extern DevelopmentConsole devConsole;
