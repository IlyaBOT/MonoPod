#include "UI.h"
#include "Config.h"
#include "Display.h"
#include "Library.h"
#include "AudioEngine.h"
#include "WiFiSync.h"
#include "Settings.h"
#include "ClockService.h"
#include "Input.h"
#include "Feedback.h"
#include <time.h>
PlayerUI ui;
void PlayerUI::begin(){page=Page::Home;lastTouch=millis();build();paint();}
void PlayerUI::navigate(Page next){
  history.push_back({page,selected,scroll,activeCategory,filter});
  page=next;selected=0;scroll=0;build();dirty=true;
}
void PlayerUI::back(){
  if(history.empty()){page=Page::Home;selected=scroll=0;build();dirty=true;return;}
  History h=history.back();history.pop_back();
  page=h.page;selected=h.selection;scroll=h.scroll;filter=h.filter;
  activeCategory=h.category;build();dirty=true;
}
bool PlayerUI::listPage()const{
  return page==Page::Home||page==Page::Music||page==Page::Artists||
         page==Page::Albums||page==Page::Genres||page==Page::Folders||
         page==Page::Songs||page==Page::Category||page==Page::Playlists||
         page==Page::Extras||page==Page::Settings||page==Page::Clicker||page==Page::SleepTimer;
}
String PlayerUI::title() const {
  switch(page){
  case Page::Home:return "iPod";case Page::Music:return "Music";
  case Page::Artists:return "Artists";case Page::Albums:return "Albums";
  case Page::Genres:return "Genres";case Page::Folders:return "Folders";
  case Page::Songs:return "Songs";case Page::Category:return filter;
  case Page::Playlists:return "Playlists";case Page::Settings:return "Settings";case Page::Clicker:return "Clicker";
  case Page::Extras:return "Extras";case Page::Clock:return "Clock";
  case Page::NowPlaying:return "Now Playing";case Page::SleepTimer:return "Sleep Timer";
  case Page::About:return "About";case Page::WiFi:return "Wi-Fi Sync";
  default:return "iPod";
  }
}
void PlayerUI::build(){
  items.clear();songIndices.clear();
  auto add=[this](const String &name,int id,bool arrow=true){items.push_back({name,id,arrow});};
  switch(page){
  case Page::Home:
    add("Music",0);add("Extras",1);add("Settings",2);add("Shuffle Songs",3,false);
    add("Backlight",4,false);if(player.active())add("Now Playing",5,false);break;
  case Page::Music:
    add("Playlists",0);add("Artists",1);add("Albums",2);add("Songs",3);
    add("Genres",4);add("Folders",5);break;
  case Page::Artists:case Page::Albums:case Page::Genres:case Page::Folders:
    activeCategory=page==Page::Artists?Category::Artist:
      page==Page::Albums?Category::Album:page==Page::Genres?Category::Genre:Category::Folder;
    {std::vector<String> names;media.categories(activeCategory,names);
      for(size_t i=0;i<names.size();i++)add(names[i],(int)i);}
    break;
  case Page::Songs:
    media.allIndices(songIndices);
    for(size_t i=0;i<songIndices.size();i++)add(media.at(songIndices[i]).title,(int)i,false);
    break;
  case Page::Category:
    media.matching(activeCategory,filter,songIndices);
    for(size_t i=0;i<songIndices.size();i++)add(media.at(songIndices[i]).title,(int)i,false);
    break;
  case Page::Playlists:
    {std::vector<String> names;media.categories(Category::Playlist,names);
     for(size_t i=0;i<names.size();i++)add(names[i],(int)i);}
    break;
  case Page::Clicker:
    for(uint8_t i=0;i<4;i++){
      ClickerMode mode=(ClickerMode)i;
      if(!feedback.available(mode))continue;
      add(String(settings.data.clicker==mode?"> ":"  ")+feedback.modeName(mode),i,false);
    }
    break;
  case Page::Extras:
    add("Clock",0);add("Sleep Timer",1);add("About",2);break;
  case Page::SleepTimer:
    add("Off",0,false);add("15 minutes",15,false);add("30 minutes",30,false);
    add("60 minutes",60,false);add("120 minutes",120,false);break;
  case Page::Settings:
    add(String("Shuffle: ")+(settings.data.shuffle?"Songs":"Off"),0,false);
    add(String("Repeat: ")+(settings.data.repeat==RepeatMode::Off?"Off":settings.data.repeat==RepeatMode::One?"One":"All"),1,false);
    add("Volume: "+String(settings.data.volume)+"%",2);
    add("Backlight: "+String(settings.data.backlightSeconds)+"s",3);
    add("Palette: "+String(settings.data.contrast+1),4,false);
    add("Audio: "+String(player.bluetooth()?"Bluetooth":"Wired"),5,false);
    add("Clicker",6);
    add("Wi-Fi Sync",7);
    add("Rescan Music",8,false);
    add("About",9);break;
  default:break;
  }
  if(selected>=(int)items.size())selected=max(0,(int)items.size()-1);
  if(scroll>selected)scroll=selected;
}
void PlayerUI::chooseTrack(size_t idx,const std::vector<size_t>&q){
  if(player.play(idx,q))navigate(Page::NowPlaying);
  else {Serial.println(player.errorText());dirty=true;}
}
void PlayerUI::open(){
  if(page==Page::NowPlaying){player.pauseToggle();dirty=true;return;}
  if(page==Page::WiFi){if(wifiSync.active())wifiSync.end();else wifiSync.begin();dirty=true;return;}
  if(page==Page::Clock||page==Page::About)return;
  if(page==Page::Home){
    switch(selected){
      case 0:navigate(Page::Music);break;
      case 1:navigate(Page::Extras);break;
      case 2:navigate(Page::Settings);break;
      case 3:{std::vector<size_t> all;media.allIndices(all);
        if(!all.empty()){settings.data.shuffle=true;settings.markDirty();chooseTrack(all[random(all.size())],all);}break;}
      case 4:blank=true;display.fillRect(0,0,160,128,0x0000);dirty=true;break;
      case 5:navigate(Page::NowPlaying);break;
    }return;
  }
  if(page==Page::Music){
    switch(selected){case 0:navigate(Page::Playlists);break;
    case 1:navigate(Page::Artists);break;case 2:navigate(Page::Albums);break;
    case 3:navigate(Page::Songs);break;case 4:navigate(Page::Genres);break;
    case 5:navigate(Page::Folders);break;}
    return;
  }
  if(page==Page::Playlists||page==Page::Artists||page==Page::Albums||page==Page::Genres||page==Page::Folders){
    if(selected>=0 && selected<(int)items.size()){
      if(page==Page::Playlists)activeCategory=Category::Playlist;
      filter=items[selected].title;navigate(Page::Category);
    }return;
  }
  if(page==Page::Songs||page==Page::Category){
    if(selected>=0 && selected<(int)songIndices.size())chooseTrack(songIndices[selected],songIndices);
    return;
  }
  if(page==Page::Extras){
    if(selected==0)navigate(Page::Clock);
    if(selected==1)navigate(Page::SleepTimer);
    if(selected==2)navigate(Page::About);return;
  }
  if(page==Page::SleepTimer){
    int minutes=items[selected].id;
    sleepDeadline=minutes?millis()+(uint32_t)minutes*60000UL:0;
    back();return;
  }
  if(page==Page::Clicker){
    if(selected>=0 && selected<(int)items.size()) {
      feedback.setMode((ClickerMode)items[selected].id);
      back();
    }
    return;
  }
  if(page==Page::Settings){
    switch(selected){
    case 0:settings.data.shuffle=!settings.data.shuffle;settings.markDirty();break;
    case 1:settings.data.repeat=(RepeatMode)(((int)settings.data.repeat+1)%3);settings.markDirty();break;
    case 2:if(player.active()){navigate(Page::NowPlaying);return;}
      settings.data.volume=(settings.data.volume+10)%(Config::MAX_VOLUME+1);
      player.setVolume(settings.data.volume);settings.markDirty();break;
    case 3:{const uint8_t modes[]={0,5,10,20,30,60};int found=0;
      for(int i=0;i<6;i++)if(settings.data.backlightSeconds==modes[i])found=i;
      settings.data.backlightSeconds=modes[(found+1)%6];settings.markDirty();break;}
    case 4:settings.data.contrast=(settings.data.contrast+1)%4;
      settings.markDirty();display.palette(settings.data.contrast);break;
    case 5:player.switchBluetooth(!player.bluetooth());break;
    case 6:navigate(Page::Clicker);return;
    case 7:navigate(Page::WiFi);return;
    case 8:player.stop();media.rescan();break;
    case 9:navigate(Page::About);return;
    }build();dirty=true;
  }
}
void PlayerUI::adjust(int delta){
  if(page==Page::NowPlaying){
    uint8_t before=settings.data.volume;
    settings.data.volume=constrain((int)settings.data.volume+delta*2,0,(int)Config::MAX_VOLUME);
    if(before!=settings.data.volume){
      player.setVolume(settings.data.volume);
      settings.markDirty();
      feedback.fire(FeedbackEvent::Scroll);
      dirty=true;
    }
    return;
  }
  if(!listPage()||items.empty())return;
  int before=selected;
  selected=constrain(selected+delta,0,(int)items.size()-1);
  if(selected!=before)feedback.fire(FeedbackEvent::Scroll);
  if(selected<scroll)scroll=selected;
  if(selected>=scroll+7)scroll=selected-6;
  dirty=true;
}
void PlayerUI::event(Key key){
  if(key==Key::None)return;
  lastTouch=millis();
  if(blank){blank=false;dirty=true;return;}
  if(key==Key::HoldChanged){dirty=true;return;}
  if(input.held())return;
  switch(key){
    case Key::Up:adjust(-1);break;
    case Key::Down:adjust(1);break;
    case Key::Menu:feedback.fire(FeedbackEvent::Menu);back();break;
    case Key::Select:open();feedback.fire(FeedbackEvent::Select);break;
    case Key::Previous:
      if(player.active()){player.previous();feedback.fire(FeedbackEvent::Transport);dirty=true;}
      else adjust(-1);
      break;
    case Key::Next:
      if(player.active()){player.next();feedback.fire(FeedbackEvent::Transport);dirty=true;}
      else adjust(1);
      break;
    case Key::PlayPause:player.pauseToggle();feedback.fire(FeedbackEvent::Transport);dirty=true;break;
    default:break;
  }
}
void PlayerUI::paint(){
  display.clear();
  display.header(title(),input.held(),player.active()&&!player.paused(),wifiSync.active());
  if(listPage()){
    if(items.empty()) display.text(8,50,media.sdOK()?"No items":"Insert microSD",display.foreground(),display.background(),18);
    for(int i=0;i<7;i++){
      int index=scroll+i;
      if(index>=(int)items.size())break;
      display.listRow(i,items[index].title,index==selected,items[index].submenu);
    }
    if(player.active()) display.footer("Now Playing >");
    else display.footer(String(items.size())+" items");
  }else if(page==Page::NowPlaying){
    if(player.active()){
      const Track &t=media.at(player.current());
      display.nowPlaying(t.title,t.artist,player.paused(),player.approximateProgress(),
                         player.elapsedSeconds(),settings.data.volume);
      display.footer("Menu: back");
    }else{
      display.text(8,53,"No music playing",display.foreground(),display.background(),18);
      display.footer("Select: play/pause");
    }
  }else if(page==Page::Clock){
    display.clock(clockService.dateTime());
    display.footer("Set via Wi-Fi Sync");
  }else if(page==Page::WiFi){
    display.text(6,25,Config::WIFI_AP_SSID,display.foreground(),display.background(),18);
    display.text(6,43,wifiSync.ip(),display.foreground(),display.background(),18);
    display.text(6,68,wifiSync.active()?"Select: Stop":"Select: Start",display.foreground(),display.background());
    display.footer("HTTP  192.168.4.1");
  }else if(page==Page::About){
    display.text(5,24,"MonoPod firmware",display.foreground(),display.background());
    display.text(5,39,"v0.4 | ESP32",display.foreground(),display.background());
    display.text(5,54,"160x128 Mono UI",display.foreground(),display.background());
    display.text(5,69,"Tracks: "+String(media.size()),display.foreground(),display.background());
    display.text(5,84,media.sdOK()?"microSD: OK":"microSD: missing",display.foreground(),display.background());
    display.footer("Open Source / DIY");
  }
  dirty=false;lastRender=millis();
}
void PlayerUI::loop(){
  if(sleepDeadline && (int32_t)(millis()-sleepDeadline)>=0){sleepDeadline=0;player.stop();dirty=true;}
  // TFT LED cannot be powered down by firmware with the current pin map.
  // This screensaver blanks pixels only; a backlight transistor is needed later.
  if(!blank && settings.data.backlightSeconds &&
     millis()-lastTouch>(uint32_t)settings.data.backlightSeconds*1000UL){
    blank=true;display.fillRect(0,0,160,128,0x0000);
  }
  if(!blank && (dirty || (page==Page::NowPlaying && millis()-lastRender>1000UL) ||
     (page==Page::Clock && millis()-lastRender>5000UL)))paint();
}
