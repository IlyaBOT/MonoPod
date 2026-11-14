#include "UI.h"
#include "Config.h"
#include "Display.h"
#include "Backlight.h"
#include "Library.h"
#include "AudioEngine.h"
#if FEATURE_BLUETOOTH
#include "BluetoothOutput.h"
#endif
#include "WiFiSync.h"
#include "WiFiNetworks.h"
#include "Settings.h"
#include "ClockService.h"
#include "Input.h"
#include "DevConsole.h"
#include "Feedback.h"
#include "MarqueeMotion.h"
#if !EXCLUDE_BAD_APPLE_EASTEREGG
#include "BadApple.h"
#endif
#include "SyncMode.h"
#include "UsbSync.h"
#include "IpodIdentity.h"
#include <time.h>
PlayerUI ui;
void PlayerUI::begin(){wheelBurst=0;lastWheelTurnAt=0;page=Page::Home;lastHiddenTrack=player.active()?(int)player.current():-1;selectedSince=nowTitleSince=millis();marqueeSelection=-1;marqueeTitle="";lastMarqueeFrame=lastTitleFrame=UINT32_MAX;lastWiFiIndicator=255;lastPlaybackState=-1;blank=false;brightnessEditing=false;lastTouch=millis();backlight.wake();build();paint();}
void PlayerUI::notify(const String& msg){blank=false;lastTouch=millis();backlight.wake();showMessage(msg);}
void PlayerUI::navigate(Page next){
  history.push_back({page,selected,scroll,activeCategory,filter});
  if(page==Page::WiFiNetworks && next!=Page::WiFiNetworks)wifiNetworks.leave();
  page=next;selected=0;scroll=0;selectedSince=nowTitleSince=millis();marqueeSelection=-1;marqueeTitle="";lastMarqueeFrame=lastTitleFrame=UINT32_MAX;brightnessEditing=false;nowOverlay=NowOverlay::Progress;seekPreview=-1;seekPreviewSeconds=0;lastSeekCommittedSeconds=UINT32_MAX;wheelBurst=0;lastWheelTurnAt=0;lastNowTime=UINT32_MAX;lastNowTrack=-1;
  if(page==Page::WiFiNetworks)wifiNetworks.enter();
#if FEATURE_BLUETOOTH
  if(page==Page::Bluetooth){BluetoothAudioOutput::ensureStarted();}
  if(page==Page::BluetoothScan){BluetoothAudioOutput::ensureStarted();BluetoothAudioOutput::startScan();btLastScan=millis();}
#endif
  if(page==Page::BadApple){
#if !EXCLUDE_BAD_APPLE_EASTEREGG
    player.stop();
    if(!badApple.begin()){const String reason=badApple.error();back();showMessage(reason);return;}
#endif
  }
  build();dirty=true;
}
void PlayerUI::back(){
#if !EXCLUDE_BAD_APPLE_EASTEREGG
  if(page==Page::BadApple)badApple.end();
#endif
  brightnessEditing=false;
  if(page==Page::WiFiNetworks)wifiNetworks.leave();
  if(history.empty()){page=Page::Home;selected=scroll=0;build();dirty=true;return;}
  History h=history.back();history.pop_back();
  page=h.page;selected=h.selection;scroll=h.scroll;filter=h.filter;
  if(page==Page::WiFiNetworks)wifiNetworks.enter();
  activeCategory=h.category;build();dirty=true;
}
void PlayerUI::showMessage(const String& msg){toast=msg;toastDeadline=millis()+2400;dirty=true;}
bool PlayerUI::listPage()const{
  switch(page){
  case Page::Home:case Page::Music:case Page::Playlists:case Page::Artists:
  case Page::Albums:case Page::Genres:case Page::Songs:case Page::Folders:
  case Page::Category:case Page::Extras:case Page::Settings:case Page::Backlight:case Page::Clicker:case Page::FontSmoothing:
  case Page::SleepTimer:case Page::WiFi:case Page::WiFiNetworks:case Page::Storage:
  case Page::BacklightTimer:case Page::ResetAllSettings:case Page::About:return true;
#if FEATURE_BLUETOOTH
  case Page::Bluetooth:case Page::BluetoothPaired:case Page::BluetoothScan:return true;
#endif
  default:return false;
  }
}
String PlayerUI::title()const{
  switch(page){
  case Page::Home:return "iPod";case Page::Music:return "Music";
  case Page::Artists:return "Artists";case Page::Albums:return "Albums";
  case Page::Genres:return "Genres";case Page::Folders:return "Folders";
  case Page::Songs:return "Songs";case Page::Category:return filter;
  case Page::Playlists:return "Playlists";case Page::Settings:return "Settings";case Page::Backlight:return "Backlight";
  case Page::Clicker:return "Clicker";case Page::BacklightTimer:return "Backlight Timer";
  case Page::ResetAllSettings:return "Reset All Settings";case Page::FontSmoothing:return "Font Smoothing";case Page::Extras:return "Extras";
  case Page::Clock:return "Clock";case Page::NowPlaying:return "Now Playing";
  case Page::SleepTimer:return "Sleep Timer";case Page::About:return "About";case Page::BadApple:return "Bad Apple";
  case Page::WiFi:case Page::WiFiNetworks:case Page::WiFiPassword:return "Wi-Fi";
#if FEATURE_BLUETOOTH
  case Page::Bluetooth:return "Bluetooth";
  case Page::BluetoothPaired:return "Paired Devices";
  case Page::BluetoothScan:return "Scan and Pair";
  case Page::BluetoothPin:return "Enter PIN";
  case Page::BluetoothPairConfirm:return "Bluetooth Pair";
  case Page::BluetoothDelete:return "Remove Device";
#endif
  case Page::Storage:case Page::StorageEject:case Page::StorageFormat1:
  case Page::StorageFormat2:return "microSD";
  case Page::DeleteTrackConfirm:return "Delete Song";
  }
  return "iPod";
}
void PlayerUI::build(){
  items.clear();songIndices.clear();
  auto add=[this](const String& name,int id,bool arrow=true){items.push_back({name,id,arrow});};
  switch(page){
  case Page::Home:
    add("Music",0);add("Extras",1);add("Settings",2);
    add("Shuffle Songs",3,false);add("Backlight",4,false);
    // Do not display a stale Now Playing entry when playback is stopped or paused.
    // Home is rebuilt whenever playState changes (0=stopped, 1=playing, 2=paused).
    if(player.active() && !player.paused())add("Now Playing",5,true);
    break;
  case Page::Music:
    add("Playlists",0);add("Artists",1);add("Albums",2);
    add("Songs",3);add("Genres",4);add("Folders",5);break;
  case Page::Artists:case Page::Albums:case Page::Genres:case Page::Folders:{
    activeCategory=page==Page::Artists?Category::Artist:page==Page::Albums?Category::Album:
                   page==Page::Genres?Category::Genre:Category::Folder;
    std::vector<String> names;media.categories(activeCategory,names);
    for(size_t i=0;i<names.size();i++)add(names[i],(int)i);
    break;
  }
  case Page::Songs:
    // Keep only indices, NOT a second String copy for every track title.
    // Rows are resolved lazily in paint(); important for 1024+ songs.
    media.allIndices(songIndices);
    break;
  case Page::Category:
    media.matching(activeCategory,filter,songIndices);
    break;
  case Page::Playlists:{
    std::vector<String> names;media.categories(Category::Playlist,names);
    for(size_t i=0;i<names.size();i++)add(names[i],(int)i);
    break;
  }
  case Page::Backlight:
    add(Config::TFT_BL_BRIGHTNESS_CONTROL
        ? String("Brightness: ")+String(settings.data.backlightBrightness)+"%"+(brightnessEditing?" *":"")
        : String("Brightness: Fixed 100%"),0,false);
    break;
  case Page::BacklightTimer:{
    const uint8_t values[]={5,10,15,30,60};
    const char* labels[]={"5 Seconds","10 Seconds","15 Seconds","30 Seconds","60 Seconds"};
    for(uint8_t i=0;i<5;i++)add(labels[i],values[i],false);
    break;
  }
  case Page::ResetAllSettings:
    add("Cancel",0,false);add("Reset",1,false);break;
  case Page::About:
    add("Songs",0,false);add("Capacity",1,false);add("Available",2,false);
    add("Version",3,false);add("S/N",4,false);add("Model",5,false);
    break;
  case Page::FontSmoothing:
    add(String(settings.data.fontSmoothing?"  ":"> ")+"Off",0,false);
    add(String(settings.data.fontSmoothing?"> ":"  ")+"On",1,false);break;
  case Page::Clicker:
    for(uint8_t i=0;i<4;i++){
      ClickerMode m=(ClickerMode)i;if(!feedback.available(m))continue;
      add(String(settings.data.clicker==m?"> ":"  ")+feedback.modeName(m),i,false);
    }break;
  case Page::Extras:
    add("Clock",0);add("Sleep Timer",1);add("About",2);
#if !EXCLUDE_BAD_APPLE_EASTEREGG
    add("Bad Apple",3,false);
#endif
    break;
  case Page::SleepTimer:
    add("Off",0,false);add("15 minutes",15,false);add("30 minutes",30,false);
    add("60 minutes",60,false);add("120 minutes",120,false);break;
  case Page::Settings:
    add("About",11);
    add(String("Shuffle: ")+(settings.data.shuffle?"Songs":"Off"),0,false);
    add(String("Repeat: ")+(settings.data.repeat==RepeatMode::Off?"Off":settings.data.repeat==RepeatMode::One?"One":"All"),1,false);
    add("Backlight Timer",12);
    add("Clicker",6);
    add("Contrast",4,false);
    add("Brightness",3);
    add("Font Smoothing",5);
    add("Reset All Settings",13);
    add("Wi-Fi",7);
#if FEATURE_BLUETOOTH
    add("Bluetooth",8);
#endif
    add("microSD",9);
    add("Rescan Music",10,false);break;
  case Page::WiFi:
    add("Wi-Fi Networks",0);
    add(wifiSync.active()?"Stop Wi-Fi Sync":"Start Wi-Fi Sync",1,false);
    add("Disconnect",2,false);break;
  case Page::WiFiNetworks:
    for(size_t i=0;i<wifiNetworks.networks().size();i++){
      auto& a=wifiNetworks.networks()[i];add(a.ssid,(int)i,false);
    }break;
#if FEATURE_BLUETOOTH
  case Page::Bluetooth:
    add("Paired Devices",0);add("Scan and Pair",1);
    break;
  case Page::BluetoothPaired:
    for(uint8_t i=0;i<BluetoothAudioOutput::bondCount();i++){
      BTHeadset d=BluetoothAudioOutput::bonded(i);
      add(String(BluetoothAudioOutput::connectedTo(d.address)?"> ":"  ")+d.name,i,false);
    }
    break;
  case Page::BluetoothScan:
    for(uint8_t i=0;i<BluetoothAudioOutput::deviceCount();i++){
      BTHeadset d=BluetoothAudioOutput::discovered(i);
      if(millis()-d.seenMs<45000) add(String(d.name),i,false);
    }
    break;
#endif
  case Page::Storage:
    add(media.sdOK()?"Eject Card":"Mount Card",0,false);
#if FEATURE_SD_FORMAT
    add("Format Card",1,false);
#else
    add("Format unavailable",1,false);
#endif
    add("Rescan Music",2,false);break;
  default:break;
  }
  if(selected>=listItemCount())selected=max(0,listItemCount()-1);
  if(scroll>selected)scroll=selected;
}
int PlayerUI::listItemCount() const {
  // Songs/Category are virtual lists: item titles are read from media on demand.
  return (page==Page::Songs || page==Page::Category)
    ? static_cast<int>(songIndices.size()) : static_cast<int>(items.size());
}
void PlayerUI::chooseTrack(size_t idx,const std::vector<size_t>& q){
  if(player.play(idx,q))navigate(Page::NowPlaying);
  else showMessage(player.errorText());
}
void PlayerUI::wifiConnect(){
  // Only printable ASCII, no spaces or Cyrillic. WPA2 PSKs require 8-63 chars.
  if(wifiPassword.length()>63 || (wifiSelectedSecured && wifiPassword.length()<8)){showMessage("Use 8-63 chars");return;}
  wifiNetworks.connect(wifiSSID,wifiPassword);
  wifiPassword="";back();
}
void PlayerUI::open(){
  if(page==Page::NowPlaying){
    // SELECT toggles scrub mode; wheel no longer changes volume until timeout.
    if(nowOverlay==NowOverlay::Seek){
      if(seekPreview>=0 && seekPreviewSeconds!=lastSeekCommittedSeconds &&
         !player.seekSeconds(seekPreviewSeconds))showMessage("Seek unsupported");
      nowOverlay=NowOverlay::Progress;seekPreview=-1;
    }else{
      if(player.durationSeconds()==0){showMessage("Duration unavailable");return;}
      nowOverlay=NowOverlay::Seek;
      seekPreviewSeconds=player.elapsedSeconds();
      if(seekPreviewSeconds>=player.durationSeconds())seekPreviewSeconds=player.durationSeconds()-1;
      seekPreview=(int)((uint64_t)seekPreviewSeconds*100u/player.durationSeconds());
      lastSeekCommittedSeconds=seekPreviewSeconds;
    }
    overlayUpdatedAt=millis();updateNowPlayingOverlay();return;
  }
  if(page==Page::Clock||page==Page::About)return;
  if(page==Page::Home){
    switch(selected){
      case 0:navigate(Page::Music);break;case 1:navigate(Page::Extras);break;
      case 2:navigate(Page::Settings);break;
      case 3:{std::vector<size_t> all;media.allIndices(all);
        if(!all.empty()){settings.data.shuffle=true;settings.markDirty();chooseTrack(all[random(all.size())],all);}
        break;}
      case 4:
        if(backlight.lit()){blank=true;backlight.sleep();}
        else{blank=false;backlight.wake();}
        dirty=true;break;
      case 5:if(player.active() && !player.paused())navigate(Page::NowPlaying);break;
    }return;
  }
  if(page==Page::Music){
    const Page target[]={Page::Playlists,Page::Artists,Page::Albums,Page::Songs,Page::Genres,Page::Folders};
    if(selected>=0&&selected<6)navigate(target[selected]);return;
  }
  if(page==Page::Playlists||page==Page::Artists||page==Page::Albums||page==Page::Genres||page==Page::Folders){
    if(selected>=0&&selected<(int)items.size()){
      if(page==Page::Playlists)activeCategory=Category::Playlist;
      filter=items[selected].title;navigate(Page::Category);
    }return;
  }
  if(page==Page::Songs||page==Page::Category){
    if(selected>=0&&selected<(int)songIndices.size())chooseTrack(songIndices[selected],songIndices);
    return;
  }
  if(page==Page::Extras){
    if(selected==0)navigate(Page::Clock);
    else if(selected==1)navigate(Page::SleepTimer);
    else if(selected==2)navigate(Page::About);
#if !EXCLUDE_BAD_APPLE_EASTEREGG
    else if(selected==3){navigate(Page::BadApple);}
#endif
    return;
  }
  if(page==Page::SleepTimer){int minutes=items[selected].id;
    sleepDeadline=minutes?millis()+(uint32_t)minutes*60000UL:0;back();return;}
  if(page==Page::Backlight){
    if(!Config::TFT_BL_BRIGHTNESS_CONTROL){showMessage("Brightness fixed 100%");return;}
    brightnessEditing=true;build();dirty=true;return;
  }
  if(page==Page::BacklightTimer){
    if(selected>=0 && selected<(int)items.size()){
      settings.data.backlightSeconds=(uint8_t)items[selected].id;
      settings.markDirty();
      // Settings take effect immediately; no reboot required.
      blank=false;backlight.wake();
      back();
    }
    return;
  }
  if(page==Page::ResetAllSettings){
    if(selected==1){
      settings.resetDefaults();
      player.setVolume(settings.data.volume);
      feedback.setMode(settings.data.clicker);
      backlight.setBrightness(settings.data.backlightBrightness);
      display.palette(settings.data.contrast);
      showMessage("Settings reset");
    }
    back();return;
  }
  if(page==Page::Clicker){
    if(selected>=0&&selected<(int)items.size())feedback.setMode((ClickerMode)items[selected].id);
    back();return;
  }
  if(page==Page::WiFi){
    switch(selected){
    case 0:navigate(Page::WiFiNetworks);return;
    case 1:if(wifiSync.active())wifiSync.end();else wifiSync.begin();build();break;
    case 2:wifiNetworks.disconnect();break;
    }dirty=true;return;
  }
  if(page==Page::WiFiNetworks){
    if(selected<0||selected>=(int)wifiNetworks.networks().size())return;
    const AccessPoint& a=wifiNetworks.networks()[selected];wifiSSID=a.ssid;wifiSelectedSecured=a.secured;
    if(!a.secured){wifiNetworks.connect(wifiSSID,"");showMessage("Connecting...");return;}
    wifiPassword="";keyboardIndex=0;keyboardLower=false;navigate(Page::WiFiPassword);return;
  }
#if FEATURE_BLUETOOTH
  if(page==Page::Bluetooth){
    if(selected==0)navigate(Page::BluetoothPaired);
    else if(selected==1)navigate(Page::BluetoothScan);
    return;
  }
  if(page==Page::BluetoothPaired && selected>=0 && selected<(int)items.size()){
    BTHeadset d=BluetoothAudioOutput::bonded(items[selected].id);
    if(BluetoothAudioOutput::connectedTo(d.address)){
      BluetoothAudioOutput::disconnect();
      player.switchBluetooth(false);
      showMessage("Disconnected");
    }else if(player.switchBluetooth(true)&&BluetoothAudioOutput::connectAddress(d.address))showMessage("Connecting...");
    else showMessage("Connection failed");
    build();dirty=true;return;
  }
  if(page==Page::BluetoothScan && selected>=0 && selected<(int)items.size()){
    BTHeadset d=BluetoothAudioOutput::discovered(items[selected].id);
    if(player.switchBluetooth(true)&&BluetoothAudioOutput::connectAddress(d.address))showMessage("Pairing / connecting");
    else showMessage("Connection failed");
    dirty=true;return;
  }
#endif
  if(page==Page::Storage){
    if(selected==0){
      if(media.sdOK()){player.stop();if(wifiSync.active())wifiSync.end();media.eject();showMessage("Safe to remove");}
      else {if(media.remount())showMessage("Card mounted");else showMessage(media.lastError());}
    }else if(selected==1){
#if FEATURE_SD_FORMAT
      navigate(Page::StorageFormat1);
#else
      showMessage("Format disabled in firmware");
#endif
    }
    else if(selected==2){player.stop();media.rescan();showMessage("Library updated");}
    build();dirty=true;return;
  }
  if(page==Page::StorageFormat1){navigate(Page::StorageFormat2);return;}
  if(page==Page::StorageFormat2){
#if !FEATURE_SD_FORMAT
    showMessage("SD format disabled");back();back();return;
#endif
    player.stop();
    if(wifiSync.active())wifiSync.end();
    display.clear();display.centerText(49,"Formatting...",true);
    if(media.formatCard()){
      display.reloadTheme();showMessage("Card ready: Music/Playlists");
    }
    else showMessage(media.lastError());
    back();back();return;
  }
  if(page==Page::FontSmoothing){
    settings.data.fontSmoothing=(selected==1);settings.markDirty();back();return;
  }
  if(page==Page::Settings){
    if(selected<0||selected>=(int)items.size())return;
    switch(items[selected].id){
    case 0:settings.data.shuffle=!settings.data.shuffle;settings.markDirty();break;
    case 1:settings.data.repeat=(RepeatMode)(((int)settings.data.repeat+1)%3);settings.markDirty();break;
    case 2:break; // volume is adjusted only on Now Playing
    case 3:navigate(Page::Backlight);return;
    case 12:navigate(Page::BacklightTimer);return;
    case 13:navigate(Page::ResetAllSettings);return;
    case 4:settings.data.contrast=(settings.data.contrast+1)%4;
      settings.markDirty();display.palette(settings.data.contrast);break;
    case 5:navigate(Page::FontSmoothing);return;
    case 6:navigate(Page::Clicker);return;
    case 7:navigate(Page::WiFi);return;
#if FEATURE_BLUETOOTH
    case 8:navigate(Page::Bluetooth);return;
#endif
    case 9:navigate(Page::Storage);return;
    case 10:player.stop();media.rescan();showMessage("Library updated");break;
    case 11:navigate(Page::About);return;
    }
    build();dirty=true;
  }
}
void PlayerUI::adjust(int delta,bool physicalWheel){
  if(delta==0)return;
  const uint32_t now=millis();
  bool fast=false;
  if(physicalWheel){
    // A single quick movement must NEVER skip several songs. Acceleration
    // requires three consecutive detents, same direction and <130 ms apart.
    const int8_t dir=delta>0?1:-1;
    if(lastWheelTurnAt && (uint32_t)(now-lastWheelTurnAt)<130 && wheelDirection==dir){
      if(wheelBurst<3)++wheelBurst;
    }else wheelBurst=0;
    lastWheelTurnAt=now;wheelDirection=dir;
    fast=wheelBurst>=2;
  }else{wheelBurst=0;lastWheelTurnAt=0;}
  if(page==Page::NowPlaying){
    overlayUpdatedAt=now;
    if(nowOverlay==NowOverlay::Seek){
      const uint32_t duration=player.durationSeconds();
      if(duration==0){showMessage("Duration unavailable");return;}
      const int32_t maximum=(int32_t)min(duration-1, (uint32_t)INT32_MAX);
      // Seek precision is one *second*, independent of the track's length.
      const int32_t next=(int32_t)seekPreviewSeconds+delta*(fast?30:1);
      seekPreviewSeconds=(uint32_t)constrain(next,0,maximum);
      seekPreview=(int)((uint64_t)seekPreviewSeconds*100u/duration);
    }else{
      nowOverlay=NowOverlay::Volume;
      const int previous=(int)settings.data.volume;
      // Saturating arithmetic in signed int: no uint8 underflow or wraparound.
      const int desired=constrain(previous+delta*(fast?5:1),0,100);
      settings.data.volume=(uint8_t)desired;
      if(previous!=desired){player.setVolume(settings.data.volume);settings.markDirty();}
    }
    updateNowPlayingOverlay();return;
  }
  const int count=listItemCount();
  if(!listPage()||count==0)return;
  const int before=selected;
  selected=constrain(selected+delta*(fast?3:1),0,count-1);
  if(selected!=before){selectedSince=millis();lastMarqueeFrame=UINT32_MAX;marqueeSelection=selected;}
  if(selected<scroll)scroll=selected;
  if(selected>=scroll+Display::ROW_COUNT)scroll=selected-Display::ROW_COUNT+1;
  dirty=true;
}
static const char* const upperRows[]={"1234567890-=","QWERTYUIOP","ASDFGHJKL;'","ZXCVBNM,./"};
static int kbCount(){int x=0;for(int i=0;i<4;i++)x+=strlen(upperRows[i]);return x;}
static char kbChar(int i,bool lower){
  for(int row=0;row<4;row++){
    int n=strlen(upperRows[row]);
    if(i<n){char c=upperRows[row][i];if(lower && c>='A'&&c<='Z')c+=32;return c;}
    i-=n;
  }return 0;
}
void PlayerUI::passwordEvent(Key key){
  int count=kbCount();
  switch(key){
  case Key::Up:keyboardIndex=(keyboardIndex-1+count+2)%(count+2);dirty=true;break;
  case Key::Down:keyboardIndex=(keyboardIndex+1)%(count+2);dirty=true;break;
  case Key::Menu:keyboardLower=!keyboardLower;dirty=true;break;
  case Key::Previous:if(wifiPassword.length())wifiPassword.remove(wifiPassword.length()-1);dirty=true;break;
  case Key::Next:wifiConnect();dirty=true;break;
  case Key::PlayPause:back();break;
  case Key::Select:
    if(keyboardIndex==count){if(wifiPassword.length())wifiPassword.remove(wifiPassword.length()-1);}
    else if(keyboardIndex==count+1){wifiConnect();return;}
    else if(wifiPassword.length()<63)wifiPassword+=kbChar(keyboardIndex,keyboardLower);
    dirty=true;break;
  default:break;
  }
}
#if FEATURE_BLUETOOTH
void PlayerUI::bluetoothEvent(Key key){
  if(page==Page::BluetoothDelete){
    if(key==Key::Select || key==Key::PlayPause){
      if(BluetoothAudioOutput::removeBond(btAddress)){
        if(player.bluetooth() && !BluetoothAudioOutput::connected())player.switchBluetooth(false);
        showMessage("Pair removed");
      }else showMessage("Remove failed");
      back();
    }else if(key==Key::Menu)back();
    return;
  }
  if(page==Page::BluetoothPairConfirm){
    if(key==Key::PlayPause)btYes=true;
    else if(key==Key::Menu){BluetoothAudioOutput::cancelPairing();back();return;}
    else if(key==Key::Up || key==Key::Down){btYes=!btYes;dirty=true;return;}
    else if(key!=Key::Select)return;
    if(BluetoothAudioOutput::answerPairing(btYes))showMessage(btYes?"Pair request accepted":"Pair denied");
    else showMessage("Reply failed");
    back();return;
  }
  if(page==Page::BluetoothPin){
    if(key==Key::Menu){BluetoothAudioOutput::cancelPairing();back();return;}
    if(key==Key::Up||key==Key::Down){
      if(btDigitIndex>=0&&btDigitIndex<(int)btDigits.length()){
        char &c=btDigits[btDigitIndex];
        c='0'+((c-'0'+(key==Key::Up?9:1))%10);
      }
    }else if(key==Key::Previous){if(btDigitIndex>0)btDigitIndex--;}
    else if(key==Key::Next){
      BTPairRequest r=BluetoothAudioOutput::pairingRequest();
      if(btDigitIndex+1<(int)btDigits.length())btDigitIndex++;
      else if(btDigits.length()<r.maxDigits){btDigits+='0';btDigitIndex++;}
    }else if(key==Key::Select){
      if(BluetoothAudioOutput::answerPairing(true,btDigits)){showMessage("PIN submitted");back();}
      else showMessage("Invalid PIN length");
    }
    dirty=true;return;
  }
}
void PlayerUI::paintBluetoothDialog(){
  const BTPairRequest r=BluetoothAudioOutput::pairingRequest();
  if(page==Page::BluetoothDelete){
    display.smallText(7,34,"Forget this device?",display.foreground(),display.background());
    display.smallText(7,52,btDeviceName.substring(0,22),display.foreground(),display.background());
    display.footer("SELECT: Yes  MENU: No");return;
  }
  if(page==Page::BluetoothPin){
    display.smallText(6,28,"PIN for "+btDeviceName.substring(0,16),display.foreground(),display.background());
    display.smallText(6,44,"Digits: "+String(r.minDigits)+"-"+String(r.maxDigits),display.foreground(),display.background());
    String shown=btDigits.substring(0,16);
    for(int i=0;i<(int)shown.length();i++){
      bool focus=i==btDigitIndex;
      uint16_t bg=focus?display.selectedBackground():display.background();
      if(focus)display.fillRect(7+i*9,62,9,13,bg);
      display.smallText(8+i*9,64,String(shown[i]),focus?display.background():display.foreground(),bg);
    }
    display.smallText(6,80,String("Digit: ")+String(btDigitIndex+1)+" / "+String(btDigits.length()),display.foreground(),display.background());
    display.footer("SEL: OK  MENU: Cancel");return;
  }
  if(page==Page::BluetoothPairConfirm){
    display.smallText(6,27,"Pair with device?",display.foreground(),display.background());
    display.smallText(6,44,btDeviceName.substring(0,22),display.foreground(),display.background());
    if(r.kind==BTPairKind::ConfirmNumber){
      char code[12];snprintf(code,sizeof(code),"%06lu",(unsigned long)r.number);
      display.centerText(62,String(code),true);
    }
    display.smallText(14,86,btYes?"> YES    NO":"  YES  > NO",display.foreground(),display.background());
    display.footer("PLAY: Yes  MENU: No");
  }
}
#endif // FEATURE_BLUETOOTH
void PlayerUI::event(Key key,bool physicalWheel){
  if(key==Key::None)return;
  if(page==Page::BadApple){if(key==Key::Menu)back();return;}
  // Sync screen owns the entire input surface. Deliberate Wi-Fi exit only;
  // USB requires a host-side SYNC END (or later MSC safe eject).
  if(syncMode.active()){
    if(syncMode.is(SyncTransport::WiFi) && key==Key::SelectLong && !wifiSync.busy()){
      wifiSync.end(); // rejects exit while either HTTP upload is in flight
      media.rescan();
      begin();
    }
    return;
  }
  lastTouch=millis();
  backlight.wake();
  if(blank){blank=false;backlight.wake();return;} // TFT RAM retained; do not clear it when waking
  if(key==Key::Up || key==Key::Down)feedback.fire(FeedbackEvent::Scroll);
  if(page==Page::Backlight && brightnessEditing){
    if(key==Key::Up || key==Key::Down || key==Key::Previous || key==Key::Next){
      const int delta=(key==Key::Up || key==Key::Previous)?-5:5;
      settings.data.backlightBrightness=constrain((int)settings.data.backlightBrightness+delta,5,100);
      settings.markDirty();backlight.setBrightness(settings.data.backlightBrightness);
      build();dirty=true;
    }else if(key==Key::Select || key==Key::Menu){brightnessEditing=false;build();dirty=true;}
    return;
  }
  if(key==Key::HoldChanged){dirty=true;return;}
  if(input.held() && !devConsole.hardwareDisabled())return;
  if(page==Page::WiFiPassword){passwordEvent(key);return;}
#if FEATURE_BLUETOOTH
  if(page==Page::BluetoothPin||page==Page::BluetoothPairConfirm||page==Page::BluetoothDelete){bluetoothEvent(key);return;}
#endif
  if(page==Page::DeleteTrackConfirm){
    if(key==Key::Menu || key==Key::PlayPause){back();return;}
    if(key==Key::Select){
      player.stop();
      const bool removed=media.sdOK() && deleteTrackPath.startsWith("/Music/") &&
        SD.remove(deleteTrackPath.c_str());
      if(removed){media.rescan();history.clear();page=Page::Songs;selected=scroll=0;
        build();showMessage("Track deleted");}
      else {back();showMessage("Delete failed");}
      dirty=true;
    }
    return;
  }
  if(key==Key::SelectLong){
    if((page==Page::Songs || page==Page::Category) &&
       selected>=0 && selected<(int)songIndices.size() && media.sdOK()){
      const Track &track=media.at(songIndices[selected]);
      if(track.path.startsWith("/Music/")){
        deleteTrackPath=track.path;deleteTrackTitle=track.title;
        navigate(Page::DeleteTrackConfirm);
      }else showMessage("Only /Music tracks");
      return;
    }
#if FEATURE_BLUETOOTH
    if(page==Page::BluetoothPaired && selected>=0 && selected<(int)items.size()){
      BTHeadset d=BluetoothAudioOutput::bonded(items[selected].id);
      memcpy(btAddress,d.address,6);btDeviceName=d.name;
      navigate(Page::BluetoothDelete);
    }
#endif
    return;
  }
  if(page==Page::StorageFormat1||page==Page::StorageFormat2){
    if(key==Key::Select)open();
    else if(key==Key::Menu||key==Key::PlayPause)back();return;
  }
  switch(key){
  case Key::Up:adjust(-1,physicalWheel);break;case Key::Down:adjust(1,physicalWheel);break;
  case Key::Menu:back();break;
  case Key::Select:open();break;
  case Key::Previous:if(player.active()){player.previous();dirty=true;}else adjust(-1);break;
  case Key::Next:if(player.active()){player.next();dirty=true;}else adjust(1);break;
  case Key::PlayPause:player.pauseToggle();dirty=true;break;
  case Key::RemotePlay:if(player.active()&&player.paused())player.pauseToggle();dirty=true;break;
  case Key::RemotePause:if(player.active()&&!player.paused())player.pauseToggle();dirty=true;break;
  default:break;
  }
}
void PlayerUI::paintKeyboard(){
  const uint16_t fg=display.foreground(),bg=display.background(),sel=display.selectedBackground();
  display.smallText(7,23,"Enter Wi-Fi password",fg,bg);
  if(toastDeadline && (int32_t)(toastDeadline-millis())>0)display.smallText(6,51,toast,fg,bg);
  display.fillRect(5,37,150,16,fg);
  display.fillRect(6,38,148,14,bg);
  String tail;
  for(size_t i=0;i<wifiPassword.length();i++)tail+='*';
  if(tail.length()>18)tail=tail.substring(tail.length()-18);
  display.smallText(10,40,tail+"_",fg,bg);
  int global=0;
  for(int row=0;row<4;row++){
    String r=upperRows[row];int y=58+row*14;
    for(unsigned int i=0;i<r.length();i++,global++){
      char letter=kbChar(global,keyboardLower);
      const int x=7+(int)i*12;
      bool active=keyboardIndex==global;
      if(active)display.fillRect(x-2,y-1,12,13,sel);
      display.smallText(x,y,String(letter),active?bg:fg,active?sel:bg,1);
    }
  }
  int endIndex=kbCount();
  const bool del=keyboardIndex==endIndex,ok=keyboardIndex==endIndex+1;
  display.smallText(18,114,"DEL",del?bg:fg,del?sel:bg);
  display.smallText(98,114,"CONNECT",ok?bg:fg,ok?sel:bg);
  if(del)display.fillRect(14,113,3,13,sel);
  if(ok)display.fillRect(94,113,3,13,sel);
}
void PlayerUI::updateNowPlayingOverlay(){
  if(page!=Page::NowPlaying || !player.active() || blank)return;
  display.nowPlayingControls(nowOverlay==NowOverlay::Volume?1:nowOverlay==NowOverlay::Seek?2:0,
    player.approximateProgress(),settings.data.volume,
    seekPreview>=0?seekPreview:0,player.elapsedSeconds(),player.remainingSeconds(),player.durationSeconds()!=0,nowOverlay==NowOverlay::Seek?seekPreviewSeconds:UINT32_MAX);
  lastNowTime=player.elapsedSeconds();
  lastRender=millis();
}
void PlayerUI::paint(){
  if(page==Page::BadApple){dirty=false;lastRender=millis();return;}
  display.startFrame();
  if(syncMode.active()){
    blank=false;
    display.syncScreen(syncMode.is(SyncTransport::WiFi),
                       syncMode.is(SyncTransport::WiFi)?wifiSync.busy():usbSync.busy(),
                       syncMode.is(SyncTransport::WiFi)?wifiSync.ip():String(),
                       syncMode.is(SyncTransport::WiFi)?wifiSync.otaLine():String(),
                       syncMode.is(SyncTransport::WiFi)&&wifiSync.otaUpdating());
    display.endFrame();dirty=false;lastRender=millis();return;
  }
  display.clear();
  lastWiFiIndicator=wifiNetworks.indicator();lastBatteryBand=display.batteryBand();
  const uint8_t bars=lastWiFiIndicator==5?3:(lastWiFiIndicator==4?0:lastWiFiIndicator);
  String headerText=title();
  if(page==Page::NowPlaying && clockService.valid())headerText=clockService.shortTime();
  display.header(headerText,input.held() && !devConsole.hardwareDisabled(),player.active(),page==Page::NowPlaying?0:bars,page==Page::NowPlaying,player.paused());
  display.headerTitleMarquee(headerText,marqueeOffset(nowTitleSince,millis()));
  if(page==Page::WiFiPassword){paintKeyboard();}
#if FEATURE_BLUETOOTH
  else if(page==Page::BluetoothPin || page==Page::BluetoothPairConfirm || page==Page::BluetoothDelete){paintBluetoothDialog();}
#endif
  else if(page==Page::DeleteTrackConfirm){
    display.smallText(6,31,"Delete from microSD?",display.foreground(),display.background());
    display.smallText(6,51,deleteTrackTitle.substring(0,23),display.foreground(),display.background());
    display.smallText(6,72,"This cannot be undone",display.foreground(),display.background());
    display.footer("SELECT: Delete  MENU: No");
  }else if(page==Page::StorageFormat1||page==Page::StorageFormat2){
    display.centerText(39,page==Page::StorageFormat1?"Erase all files?":"FINAL CONFIRM",true);
    display.centerText(63,page==Page::StorageFormat1?"Cannot be undone":"SELECT: Format",true);
    display.footer("MENU: Cancel");
  }else if(listPage()){
    if(page==Page::WiFiNetworks && items.empty()){
      display.centerText(56,wifiNetworks.scanning()?"Scanning...":"No networks",false);
    }
#if FEATURE_BLUETOOTH
    else if(page==Page::BluetoothScan && items.empty()){
      display.centerText(56,BluetoothAudioOutput::scanning()?"Scanning...":"No devices",false);
    }else if(page==Page::BluetoothPaired && items.empty()){
      display.centerText(56,"No paired devices",false);
    }
#endif
    for(int i=0;i<Display::ROW_COUNT;i++){
      int index=scroll+i;if(index>=listItemCount())break;
      if(page==Page::Songs || page==Page::Category){
        display.listRow(i,media.at(songIndices[index]).title,index==selected,false,index==selected?marqueeOffset(selectedSince,millis()):0);
        continue;
      }
      if(page==Page::About){
        // 2004-style two-column information list, readable without navigating.
        const Item &entry=items[index];
        String value;
        switch(entry.id){
        case 0:value=String(media.size());break;
        case 1:value=String((unsigned long)(media.totalBytes()/(1024ULL*1024ULL)))+" MB";break;
        case 2:{uint64_t cap=media.totalBytes(),used=media.usedBytes();
          value=String((unsigned long)((cap>used?cap-used:0)/(1024ULL*1024ULL)))+" MB";break;}
        case 3:value="0.10.6.6";break;
        case 4:value=String(IpodIdentity::guid());break;
        case 5:value=MONOPOD_BOARD==MONOPOD_BOARD_ESP32_S3?"S3 N16R8":"ESP32";break;
        }
        const int rowY=23+i*18;
        display.smallText(6,rowY,entry.title,display.foreground(),display.background());
        if(entry.id==4){
          // Full 16-digit device ID, directly after S/N + exactly two small-font spaces.
          // The compact (6x8px) glyphs fit on one 160px row without ellipsis.
          const int serialX=6+display.textWidth(entry.title,true)+display.textWidth("  ",true);
          display.serialText(serialX,rowY,value,display.foreground(),display.background());
          continue;
        }
        const int valueWidth=display.textWidth(value,true);
        if(valueWidth<92){
          display.smallText(155-valueWidth,rowY,value,display.foreground(),display.background());
        }else{
          display.smallText(70,rowY,display.fittedTitle(value,82),display.foreground(),display.background());
        }
        continue;
      }
      const Item &item=items[index];
      display.listRow(i,item.title,index==selected,item.submenu,index==selected?marqueeOffset(selectedSince,millis()):0);
      if(page==Page::WiFiNetworks){
        const auto &a=wifiNetworks.networks()[item.id];
        int strength=a.rssi>=-55?3:a.rssi>=-70?2:a.rssi>=-85?1:0;
        int y=24+i*18;
        display.fillRect(133,y,23,14,index==selected?display.selectedBackground():display.background());
        for(int j=0;j<3;j++){
          int h=4+j*3;
          display.fillRect(134+j*6,y+12-h,4,h,j<strength?
            (index==selected?display.background():display.foreground()):display.muted());
        }
        if(a.secured)display.smallText(125,y+1,"*",index==selected?display.background():display.foreground(),
                                     index==selected?display.selectedBackground():display.background());
      }
    }
    if(page==Page::WiFiNetworks){
      String msg=wifiNetworks.connected()?"Connected: "+wifiNetworks.connectedSSID():
        wifiNetworks.connecting()?"Connecting...":wifiNetworks.scanning()?"Scanning...":"Select network";
      display.footer(msg);
    }
#if FEATURE_BLUETOOTH
    else if(page==Page::Bluetooth || page==Page::BluetoothPaired || page==Page::BluetoothScan){
      display.footer(BluetoothAudioOutput::connected()?"BT Connected":BluetoothAudioOutput::scanning()?"Scanning...":"SELECT: Connect");
    }
#endif
    else if(page==Page::Storage){
      // SD card pictogram + usage progress bar; actions occupy only three rows.
      uint64_t cap=media.totalBytes(),used=media.usedBytes();
      display.fillRect(8,78,16,24,display.foreground());
      display.fillRect(11,82,10,16,display.background());
      display.smallText(31,78,media.sdOK()?"Card inserted":"No card",display.foreground(),display.background());
      if(cap){display.progress(31,96,120,(int)(used*100/cap));}

      display.footer(media.sdOK()?String(used/(1024*1024))+"/"+String(cap/(1024*1024))+" MB":"No card mounted");
    }else if(page==Page::WiFi)display.footer(wifiNetworks.connected()?wifiNetworks.connectedSSID():wifiSync.ip());
    // Do not draw an item count or a generic bottom bar on normal list screens.
    // Keep the lower 14px quiet, as on the early monochrome iPod UI.
  }else if(page==Page::NowPlaying){
    if(player.active()){
      const Track& t=media.at(player.current());
      display.nowPlaying(t.title,t.artist,t.album,player.paused(),(uint32_t)player.current()+1,media.size());
      lastNowTrack=player.current();
      display.nowPlayingTitleMarquee(t.title,marqueeOffset(nowTitleSince,millis()));
      display.nowPlayingControls(nowOverlay==NowOverlay::Volume?1:nowOverlay==NowOverlay::Seek?2:0,
        player.approximateProgress(),settings.data.volume,seekPreview>=0?seekPreview:0,
        player.elapsedSeconds(),player.remainingSeconds(),player.durationSeconds()!=0,
        nowOverlay==NowOverlay::Seek?seekPreviewSeconds:UINT32_MAX);
      lastNowTime=player.elapsedSeconds();
    }else display.centerText(51,"No music playing",true);
    // Bottom rows are reserved for progress/time or volume; no footer.
  }else if(page==Page::Clock){display.clock(clockService.dateTime());display.footer("Set via Wi-Fi Sync");}
  if(toastDeadline && (int32_t)(toastDeadline-millis())>0 && page!=Page::WiFiPassword){
    display.fillRect(1,94,158,18,display.selectedBackground());
    display.smallText(6,97,toast,display.background(),display.selectedBackground());
  }
  display.endFrame();dirty=false;lastRender=millis();
}
int PlayerUI::marqueeOffset(uint32_t since,uint32_t now)const{
  return (int)MarqueeMotion::pixelOffset(since,now);
}
String PlayerUI::selectedText()const{
  if(selected<0 || selected>=listItemCount())return String();
  if(page==Page::Songs || page==Page::Category)return media.at(songIndices[selected]).title;
  return items[selected].title;
}
void PlayerUI::loop(){
#if !EXCLUDE_BAD_APPLE_EASTEREGG
  if(page==Page::BadApple){
    if(!badApple.active()){back();return;}
    badApple.loop();backlight.loop();return;
  }
#endif
  if(syncMode.active()){
    // Never blank the sync warning, navigate, run sleep timers or scan Wi-Fi.
    blank=false;backlight.wake();backlight.loop();
    if(dirty || millis()-lastRender>500UL)paint();
    return;
  }
  // Audio runs independently of the UI page; reflect play/pause on every screen.
  const int8_t playState=player.active()?(player.paused()?2:1):0;
  if(playState!=lastPlaybackState){
    lastPlaybackState=playState;
    if(page==Page::Home){build();}  // Show or hide Now Playing as playback starts/stops/pauses.
    dirty=true;
  }
  wifiNetworks.tick();
  const bool wifiChanged=wifiNetworks.changed();
  if(page==Page::WiFi && wifiChanged)dirty=true;
  if(page==Page::WiFiNetworks && wifiChanged){
    String keep=(selected>=0&&selected<(int)items.size())?items[selected].title:"";
    build();
    for(int i=0;i<(int)items.size();i++)if(items[i].title==keep){selected=i;break;}
    if(selected<scroll)scroll=selected;
    dirty=true;
  }
#if FEATURE_BLUETOOTH
  BluetoothAudioOutput::service();
  BTPairRequest req=BluetoothAudioOutput::pairingRequest();
  if(req.kind!=BTPairKind::None){
    if(millis()-req.receivedMs>30000){
      BluetoothAudioOutput::cancelPairing();
      if(page==Page::BluetoothPin||page==Page::BluetoothPairConfirm){back();showMessage("Pair timeout");}
    }else if(page!=Page::BluetoothPin&&page!=Page::BluetoothPairConfirm){
      memcpy(btAddress,req.address,6);
      btDeviceName=BluetoothAudioOutput::addressString(req.address);
      for(uint8_t i=0;i<BluetoothAudioOutput::deviceCount();i++){
        BTHeadset h=BluetoothAudioOutput::discovered(i);
        if(memcmp(h.address,req.address,6)==0){btDeviceName=h.name;break;}
      }
      btYes=false;btDigitIndex=0;
      btDigits=String('0');
      while(btDigits.length()<req.minDigits)btDigits+='0';
      navigate(req.kind==BTPairKind::ConfirmNumber?Page::BluetoothPairConfirm:Page::BluetoothPin);
      MONOPOD_LOG.printf("BT UI: pairing modal %d\n",(int)req.kind);
    }
  }
  if(page==Page::BluetoothScan && !BluetoothAudioOutput::scanning() && millis()-btLastScan>12000){
    BluetoothAudioOutput::startScan();btLastScan=millis();
  }
  if((page==Page::BluetoothScan||page==Page::BluetoothPaired) && millis()-lastNetworkUiUpdate>1500){
    lastNetworkUiUpdate=millis();
    uint8_t keep[6]={};bool hasKeep=false;
    if(selected>=0 && selected<(int)items.size()){
      BTHeadset h=page==Page::BluetoothPaired?BluetoothAudioOutput::bonded(items[selected].id):BluetoothAudioOutput::discovered(items[selected].id);
      memcpy(keep,h.address,6);hasKeep=true;
    }
    build();
    if(hasKeep)for(int i=0;i<(int)items.size();i++){
      BTHeadset h=page==Page::BluetoothPaired?BluetoothAudioOutput::bonded(items[i].id):BluetoothAudioOutput::discovered(items[i].id);
      if(memcmp(h.address,keep,6)==0){selected=i;break;}
    }
    dirty=true;
  }
#endif
  if(sleepDeadline && (int32_t)(millis()-sleepDeadline)>=0){sleepDeadline=0;player.stop();dirty=true;}
  if(toastDeadline && (int32_t)(millis()-toastDeadline)>=0){toastDeadline=0;dirty=true;}
  if(!blank &&
    millis()-lastTouch>(uint32_t)settings.data.backlightSeconds*1000UL){
    blank=true;lastHiddenRefresh=millis();backlight.sleep();
  }
  // Keep the framebuffer current behind an unlit backlight; never call wake().
  // Change of track has priority; otherwise do one refresh every 10 minutes.
  if(blank && !syncMode.active()){
    const uint32_t now=millis();
    const int track=player.active()?(int)player.current():-1;
    bool changedTrack=track!=lastHiddenTrack;
    lastHiddenTrack=track;
    if(changedTrack){nowTitleSince=now;lastTitleFrame=UINT32_MAX;}
    if(changedTrack || now-lastHiddenRefresh>=600000UL){
      paint();lastHiddenRefresh=now;
    }
  }
  if(!blank)lastHiddenTrack=player.active()?(int)player.current():-1;
  // Marquee is updated only when backlight is on. Selected rows go back to
  // their original text and ellipsis immediately when selection changes.
  if(!blank && !dirty){
    const uint32_t now=millis();
    const String heading=title();
    if(heading!=marqueeTitle){marqueeTitle=heading;nowTitleSince=now;lastTitleFrame=UINT32_MAX;}
    bool compact=display.textWidth(heading,false)>100;
    if(compact && display.textWidth(heading,true)>100){
      uint32_t frame=MarqueeMotion::pixelOffset(nowTitleSince,now);
      if(frame!=lastTitleFrame){display.headerTitleMarquee(heading,(int)frame);lastTitleFrame=frame;}
    }
    if(listPage() && page!=Page::About && page!=Page::WiFiNetworks){
      String label=selectedText();
      if(marqueeSelection!=selected){marqueeSelection=selected;selectedSince=now;lastMarqueeFrame=UINT32_MAX;}
      if(display.textWidth(label,false)>(selected<(int)items.size() && page!=Page::Songs && page!=Page::Category && items[selected].submenu?137:149)){
        uint32_t frame=MarqueeMotion::pixelOffset(selectedSince,now);
        if(frame!=lastMarqueeFrame){
          lastMarqueeFrame=frame;
          // Draw just the selected row. Avoid full-screen SPI refresh at 12 FPS.
          int row=selected-scroll;
          if(row>=0 && row<Display::ROW_COUNT){
            bool submenu=page!=Page::Songs && page!=Page::Category && selected<(int)items.size()?items[selected].submenu:false;
            display.listRow((uint8_t)row,label,true,submenu,(int)frame);
          }
        }
      }
    }
    if(page==Page::NowPlaying && player.active()){
      if(player.current()!=lastNowTrack){nowTitleSince=now;lastTitleFrame=UINT32_MAX;}
      const String& name=media.at(player.current()).title;
      if(display.textWidth(name,true)>152){
        uint32_t frame=MarqueeMotion::pixelOffset(nowTitleSince,now);
        if(frame!=lastTitleFrame){display.nowPlayingTitleMarquee(name,(int)frame);lastTitleFrame=frame;}
      }
    }
  }
  // Update only the Wi-Fi glyph while idle; do not clear/redraw the menu.
  if(!blank && !dirty && page!=Page::NowPlaying && (!player.active() || input.held())){
    const uint8_t next=wifiNetworks.indicator();
    if(next!=lastWiFiIndicator){
      lastWiFiIndicator=next;
      display.wifiSignal(next==5?3:(next==4?0:next));
    }
  }
  // Battery icon changes only when a 25% band boundary is crossed; no full screen redraw.
  if(!blank && !dirty && display.batteryBand()!=lastBatteryBand){
    lastBatteryBand=display.batteryBand();display.batteryIcon();
  }
  if(page==Page::NowPlaying && !blank && !dirty && player.current()!=lastNowTrack){nowTitleSince=millis();lastTitleFrame=UINT32_MAX;dirty=true;}
  if(page==Page::NowPlaying && player.active() && !blank && !dirty){
    const uint32_t now=millis();
    if(nowOverlay!=NowOverlay::Progress && now-overlayUpdatedAt>=5000UL){
      nowOverlay=NowOverlay::Progress;seekPreview=-1;updateNowPlayingOverlay();
    }else if(nowOverlay==NowOverlay::Seek && seekPreview>=0 && now-overlayUpdatedAt>=350UL){
      // Debounced seek. Avoid tearing down the MP3 decoder on every wheel detent.
      if(seekPreviewSeconds!=lastSeekCommittedSeconds){
        if(!player.seekSeconds(seekPreviewSeconds))showMessage("Seek unavailable");
        else lastSeekCommittedSeconds=seekPreviewSeconds;
      }
    }else if(nowOverlay==NowOverlay::Progress && player.elapsedSeconds()!=lastNowTime){
      updateNowPlayingOverlay();
    }
  }
  if(page==Page::NowPlaying && clockService.valid() && millis()-lastClockHeader>=1000UL){
    lastClockHeader=millis();
    const String next=clockService.shortTime();
    if(next!=shownClock){shownClock=next;dirty=true;}
  }
  if(!blank && (dirty || (page==Page::Clock&&millis()-lastRender>5000UL)))paint();
  backlight.loop();
}
