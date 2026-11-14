#include "UI.h"
#include "Config.h"
#include "Display.h"
#include "Library.h"
#include "AudioEngine.h"
#include "BluetoothOutput.h"
#include "WiFiSync.h"
#include "WiFiNetworks.h"
#include "Settings.h"
#include "ClockService.h"
#include "Input.h"
#include "Feedback.h"
#include <time.h>
PlayerUI ui;
void PlayerUI::begin(){page=Page::Home;lastTouch=millis();build();paint();}
void PlayerUI::navigate(Page next){
  history.push_back({page,selected,scroll,activeCategory,filter});
  page=next;selected=0;scroll=0;
  if(page==Page::WiFiNetworks)wifiNetworks.enter();
  build();dirty=true;
}
void PlayerUI::back(){
  if(page==Page::WiFiNetworks)wifiNetworks.leave();
  if(history.empty()){page=Page::Home;selected=scroll=0;build();dirty=true;return;}
  History h=history.back();history.pop_back();
  page=h.page;selected=h.selection;scroll=h.scroll;filter=h.filter;
  activeCategory=h.category;build();dirty=true;
}
void PlayerUI::showMessage(const String& msg){toast=msg;toastDeadline=millis()+2400;dirty=true;}
bool PlayerUI::listPage()const{
  switch(page){
  case Page::Home:case Page::Music:case Page::Playlists:case Page::Artists:
  case Page::Albums:case Page::Genres:case Page::Songs:case Page::Folders:
  case Page::Category:case Page::Extras:case Page::Settings:case Page::Clicker:case Page::FontSmoothing:
  case Page::SleepTimer:case Page::WiFi:case Page::WiFiNetworks:case Page::Bluetooth:
  case Page::Storage:return true;
  default:return false;
  }
}
String PlayerUI::title()const{
  switch(page){
  case Page::Home:return "iPod";case Page::Music:return "Music";
  case Page::Artists:return "Artists";case Page::Albums:return "Albums";
  case Page::Genres:return "Genres";case Page::Folders:return "Folders";
  case Page::Songs:return "Songs";case Page::Category:return filter;
  case Page::Playlists:return "Playlists";case Page::Settings:return "Settings";
  case Page::Clicker:return "Clicker";case Page::FontSmoothing:return "Font Smoothing";case Page::Extras:return "Extras";
  case Page::Clock:return "Clock";case Page::NowPlaying:return "Now Playing";
  case Page::SleepTimer:return "Sleep Timer";case Page::About:return "About";
  case Page::WiFi:case Page::WiFiNetworks:case Page::WiFiPassword:return "Wi-Fi";
  case Page::Bluetooth:return "Bluetooth";
  case Page::Storage:case Page::StorageEject:case Page::StorageFormat1:
  case Page::StorageFormat2:return "microSD";
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
    if(player.active())add("Now Playing",5,false);break;
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
    media.allIndices(songIndices);
    for(size_t i=0;i<songIndices.size();i++)add(media.at(songIndices[i]).title,(int)i,false);
    break;
  case Page::Category:
    media.matching(activeCategory,filter,songIndices);
    for(size_t i=0;i<songIndices.size();i++)add(media.at(songIndices[i]).title,(int)i,false);
    break;
  case Page::Playlists:{
    std::vector<String> names;media.categories(Category::Playlist,names);
    for(size_t i=0;i<names.size();i++)add(names[i],(int)i);
    break;
  }
  case Page::FontSmoothing:
    add(String(settings.data.fontSmoothing?"  ":"> ")+"Off",0,false);
    add(String(settings.data.fontSmoothing?"> ":"  ")+"On",1,false);break;
  case Page::Clicker:
    for(uint8_t i=0;i<4;i++){
      ClickerMode m=(ClickerMode)i;if(!feedback.available(m))continue;
      add(String(settings.data.clicker==m?"> ":"  ")+feedback.modeName(m),i,false);
    }break;
  case Page::Extras:add("Clock",0);add("Sleep Timer",1);add("About",2);break;
  case Page::SleepTimer:
    add("Off",0,false);add("15 minutes",15,false);add("30 minutes",30,false);
    add("60 minutes",60,false);add("120 minutes",120,false);break;
  case Page::Settings:
    add(String("Shuffle: ")+(settings.data.shuffle?"Songs":"Off"),0,false);
    add(String("Repeat: ")+(settings.data.repeat==RepeatMode::Off?"Off":settings.data.repeat==RepeatMode::One?"One":"All"),1,false);
    add("Volume: "+String(settings.data.volume)+"%",2);
    add("Backlight: "+String(settings.data.backlightSeconds)+"s",3);
    add("Palette: "+String(settings.data.contrast+1),4,false);
    add("Font Smoothing",5);
    add("Clicker",6);add("Wi-Fi",7);add("Bluetooth",8);add("microSD",9);
    add("Rescan Music",10,false);add("About",11);break;
  case Page::WiFi:
    add("Wi-Fi Networks",0);
    add(wifiSync.active()?"Stop Wi-Fi Sync":"Start Wi-Fi Sync",1,false);
    add("Disconnect",2,false);break;
  case Page::WiFiNetworks:
    for(size_t i=0;i<wifiNetworks.networks().size();i++){
      auto& a=wifiNetworks.networks()[i];add(a.ssid,(int)i,false);
    }break;
  case Page::Bluetooth:
#if FEATURE_BLUETOOTH
    add(player.bluetooth()?"Wired Output":"Enable Bluetooth",-1,false);
    for(uint8_t i=0;i<BluetoothAudioOutput::deviceCount();i++){
      BTHeadset d=BluetoothAudioOutput::discovered(i);add(String(d.name),(int)i,false);
    }
#else
    add("Enable in Config.h",-2,false);
#endif
    break;
  case Page::Storage:
    add(media.sdOK()?"Eject Card":"Mount Card",0,false);
    add("Format Card",1,false);
    add("Rescan Music",2,false);break;
  default:break;
  }
  if(selected>=(int)items.size())selected=max(0,(int)items.size()-1);
  if(scroll>selected)scroll=selected;
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
  if(page==Page::NowPlaying){player.pauseToggle();dirty=true;return;}
  if(page==Page::Clock||page==Page::About)return;
  if(page==Page::Home){
    switch(selected){
      case 0:navigate(Page::Music);break;case 1:navigate(Page::Extras);break;
      case 2:navigate(Page::Settings);break;
      case 3:{std::vector<size_t> all;media.allIndices(all);
        if(!all.empty()){settings.data.shuffle=true;settings.markDirty();chooseTrack(all[random(all.size())],all);}
        break;}
      case 4:blank=true;display.fillRect(0,0,160,128,0);break;
      case 5:navigate(Page::NowPlaying);break;
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
    else if(selected==2)navigate(Page::About);return;
  }
  if(page==Page::SleepTimer){int minutes=items[selected].id;
    sleepDeadline=minutes?millis()+(uint32_t)minutes*60000UL:0;back();return;}
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
  if(page==Page::Bluetooth){
#if FEATURE_BLUETOOTH
    if(selected==0){if(player.switchBluetooth(!player.bluetooth()))build();else showMessage(player.errorText());}
    else if(player.bluetooth() && selected>0){
      if(BluetoothAudioOutput::connectDevice(selected-1))showMessage("Connecting...");
      else showMessage("Connect failed");
    }else showMessage("Enable Bluetooth first");
#else
    showMessage("Bluetooth disabled");
#endif
    dirty=true;return;
  }
  if(page==Page::Storage){
    if(selected==0){
      if(media.sdOK()){player.stop();if(wifiSync.active())wifiSync.end();media.eject();showMessage("Safe to remove");}
      else {if(media.remount())showMessage("Card mounted");else showMessage(media.lastError());}
    }else if(selected==1)navigate(Page::StorageFormat1);
    else if(selected==2){player.stop();media.rescan();showMessage("Library updated");}
    build();dirty=true;return;
  }
  if(page==Page::StorageFormat1){navigate(Page::StorageFormat2);return;}
  if(page==Page::StorageFormat2){
    player.stop();
    if(wifiSync.active())wifiSync.end();
    if(media.formatCard())showMessage("Format complete");
    else showMessage(media.lastError());
    back();back();return;
  }
  if(page==Page::FontSmoothing){
    settings.data.fontSmoothing=(selected==1);settings.markDirty();back();return;
  }
  if(page==Page::Settings){
    switch(selected){
    case 0:settings.data.shuffle=!settings.data.shuffle;settings.markDirty();break;
    case 1:settings.data.repeat=(RepeatMode)(((int)settings.data.repeat+1)%3);settings.markDirty();break;
    case 2:settings.data.volume=(settings.data.volume+10)%(Config::MAX_VOLUME+1);
      player.setVolume(settings.data.volume);settings.markDirty();break;
    case 3:{const uint8_t modes[]={0,5,10,20,30,60};int at=0;
      for(int i=0;i<6;i++)if(settings.data.backlightSeconds==modes[i])at=i;
      settings.data.backlightSeconds=modes[(at+1)%6];settings.markDirty();break;}
    case 4:settings.data.contrast=(settings.data.contrast+1)%4;
      settings.markDirty();display.palette(settings.data.contrast);break;
    case 5:navigate(Page::FontSmoothing);return;
    case 6:navigate(Page::Clicker);return;
    case 7:navigate(Page::WiFi);return;
    case 8:navigate(Page::Bluetooth);return;
    case 9:navigate(Page::Storage);return;
    case 10:player.stop();media.rescan();showMessage("Library updated");break;
    case 11:navigate(Page::About);return;
    }
    build();dirty=true;
  }
}
void PlayerUI::adjust(int delta){
  if(page==Page::NowPlaying){
    uint8_t before=settings.data.volume;
    settings.data.volume=constrain((int)settings.data.volume+delta*2,0,(int)Config::MAX_VOLUME);
    if(before!=settings.data.volume){player.setVolume(settings.data.volume);settings.markDirty();feedback.fire(FeedbackEvent::Scroll);dirty=true;}
    return;
  }
  if(!listPage()||items.empty())return;
  int before=selected;selected=constrain(selected+delta,0,(int)items.size()-1);
  if(selected!=before)feedback.fire(FeedbackEvent::Scroll);
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
    feedback.fire(FeedbackEvent::Select);dirty=true;break;
  default:break;
  }
}
void PlayerUI::event(Key key){
  if(key==Key::None)return;lastTouch=millis();
  if(blank){blank=false;dirty=true;return;}
  if(key==Key::HoldChanged){dirty=true;return;}
  if(input.held())return;
  if(page==Page::WiFiPassword){passwordEvent(key);return;}
  if(page==Page::StorageFormat1||page==Page::StorageFormat2){
    if(key==Key::Select)open();
    else if(key==Key::Menu||key==Key::PlayPause)back();return;
  }
  switch(key){
  case Key::Up:adjust(-1);break;case Key::Down:adjust(1);break;
  case Key::Menu:feedback.fire(FeedbackEvent::Menu);back();break;
  case Key::Select:open();feedback.fire(FeedbackEvent::Select);break;
  case Key::Previous:if(player.active()){player.previous();dirty=true;}else adjust(-1);break;
  case Key::Next:if(player.active()){player.next();dirty=true;}else adjust(1);break;
  case Key::PlayPause:player.pauseToggle();dirty=true;break;
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
void PlayerUI::paint(){
  display.clear();
  display.header(title(),input.held(),player.active()&&!player.paused(),wifiNetworks.connected());
  if(page==Page::WiFiPassword){paintKeyboard();}
  else if(page==Page::StorageFormat1||page==Page::StorageFormat2){
    display.centerText(39,page==Page::StorageFormat1?"Erase all files?":"FINAL CONFIRM",true);
    display.centerText(63,page==Page::StorageFormat1?"Cannot be undone":"SELECT: Format",true);
    display.footer("MENU: Cancel");
  }else if(listPage()){
    if(page==Page::WiFiNetworks && items.empty()){
      display.centerText(56,wifiNetworks.scanning()?"Scanning...":"No networks",false);
    }else if(page==Page::Bluetooth && items.size()<=1){
      display.centerText(72,"Scanning...",true);
    }
    for(int i=0;i<Display::ROW_COUNT;i++){
      int index=scroll+i;if(index>=(int)items.size())break;
      const Item &item=items[index];
      display.listRow(i,item.title,index==selected,item.submenu);
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
    }else if(page==Page::Bluetooth){
#if FEATURE_BLUETOOTH
      display.footer(BluetoothAudioOutput::connected()?"Connected":BluetoothAudioOutput::scanning()?"Scanning...":"Select headset");
#else
      display.footer("BT disabled in config");
#endif
    }else if(page==Page::Storage){
      // SD card pictogram + usage progress bar; actions occupy only three rows.
      uint64_t cap=media.totalBytes(),used=media.usedBytes();
      display.fillRect(8,78,16,24,display.foreground());
      display.fillRect(11,82,10,16,display.background());
      display.smallText(31,78,media.sdOK()?"Card inserted":"No card",display.foreground(),display.background());
      if(cap){display.progress(31,96,120,(int)(used*100/cap));}

      display.footer(media.sdOK()?String(used/(1024*1024))+"/"+String(cap/(1024*1024))+" MB":"No card mounted");
    }else if(page==Page::WiFi)display.footer(wifiNetworks.connected()?wifiNetworks.connectedSSID():wifiSync.ip());
    else if(player.active())display.footer("Now Playing >");
    else display.footer(String(items.size())+" items");
  }else if(page==Page::NowPlaying){
    if(player.active()){
      const Track& t=media.at(player.current());
      display.nowPlaying(t.title,t.artist,player.paused(),player.approximateProgress(),player.elapsedSeconds(),settings.data.volume);
    }else display.centerText(51,"No music playing",true);
    display.footer("MENU: Back");
  }else if(page==Page::Clock){display.clock(clockService.dateTime());display.footer("Set via Wi-Fi Sync");}
  else if(page==Page::About){
    display.smallText(7,27,"MonoPod v0.6",display.foreground(),display.background());
    display.smallText(7,44,"ESP32-WROVER",display.foreground(),display.background());
    display.smallText(7,61,"160x128 UI",display.foreground(),display.background());
    display.smallText(7,78,"Tracks: "+String(media.size()),display.foreground(),display.background());
    display.footer("Open Source / DIY");
  }
  if(toastDeadline && (int32_t)(toastDeadline-millis())>0 && page!=Page::WiFiPassword){
    display.fillRect(1,94,158,18,display.selectedBackground());
    display.smallText(6,97,toast,display.background(),display.selectedBackground());
  }
  dirty=false;lastRender=millis();
}
void PlayerUI::loop(){
  wifiNetworks.tick();
  if(page==Page::WiFiNetworks && wifiNetworks.changed()){
    String keep=(selected>=0&&selected<(int)items.size())?items[selected].title:"";
    build();
    for(int i=0;i<(int)items.size();i++)if(items[i].title==keep){selected=i;break;}
    if(selected<scroll)scroll=selected;
    dirty=true;
  }
#if FEATURE_BLUETOOTH
  if(page==Page::Bluetooth && millis()-lastNetworkUiUpdate>1500){
    lastNetworkUiUpdate=millis();build();dirty=true;
  }
#endif
  if(sleepDeadline && (int32_t)(millis()-sleepDeadline)>=0){sleepDeadline=0;player.stop();dirty=true;}
  if(toastDeadline && (int32_t)(millis()-toastDeadline)>=0){toastDeadline=0;dirty=true;}
  if(!blank && settings.data.backlightSeconds &&
    millis()-lastTouch>(uint32_t)settings.data.backlightSeconds*1000UL){
    blank=true;display.fillRect(0,0,160,128,0);
  }
  if(!blank && (dirty || (page==Page::NowPlaying&&millis()-lastRender>1000UL) ||
    (page==Page::Clock&&millis()-lastRender>5000UL)))paint();
}
