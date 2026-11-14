#pragma once
#include "Types.h"
#include <Preferences.h>
class SettingsStore {
public:
  SettingsState data;
  void begin();
  void markDirty();
  void loop();
  void saveNow();
private:
  Preferences pref;
  bool dirty=false;
  uint32_t updated=0;
};
extern SettingsStore settings;
