#include "Settings.h"
#include "Config.h"
SettingsStore settings;
void SettingsStore::begin() {
  pref.begin("monopod",false);
  data.volume=constrain((int)pref.getUChar("vol",30),0,100);
  data.shuffle=pref.getBool("shuffle",false);
  data.repeat=(RepeatMode)constrain((int)pref.getUChar("repeat",0),0,2);
  data.backlightSeconds=pref.getUChar("bklight",10);
  data.backlightBrightness=constrain((int)pref.getUChar("blpct",75),5,100);
  // v0.4 new key: do NOT read legacy clickmode (it encoded hardware Piezo/Haptic).
  data.clicker=(ClickerMode)constrain((int)pref.getUChar("clickroute",(uint8_t)ClickerMode::Speaker),0,3);
  data.fontSmoothing=pref.getBool("fontAA",false);
  data.contrast=constrain((int)pref.getUChar("palette",2),0,3);
}
void SettingsStore::markDirty() { dirty=true;updated=millis(); }
void SettingsStore::loop() {if(dirty && millis()-updated>=1800) saveNow();}
void SettingsStore::saveNow() {
  if(!dirty) return;
  pref.putUChar("vol",data.volume);pref.putBool("shuffle",data.shuffle);
  pref.putUChar("repeat",(uint8_t)data.repeat);
  pref.putUChar("bklight",data.backlightSeconds);
  pref.putUChar("blpct",data.backlightBrightness);
  pref.putUChar("clickroute",(uint8_t)data.clicker);
  pref.putUChar("palette",data.contrast);pref.putBool("fontAA",data.fontSmoothing);
  dirty=false;
}

void SettingsStore::resetDefaults(){
  data=SettingsState{};
  markDirty();saveNow();
}
