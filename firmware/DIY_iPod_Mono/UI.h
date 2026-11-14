#pragma once
#include "Types.h"
#include <vector>
class PlayerUI {
public:
  void begin();void event(Key key);void loop();
  void redraw(){dirty=true;}
  void notify(const String& text);
private:
  String shownClock="";
  uint32_t lastClockHeader=0;
  struct Item {String title;int id;bool submenu;};
  struct History {Page page;int selection;int scroll;Category category;String filter;};
  Page page=Page::Home;
  Category activeCategory=Category::Artist;
  String filter;
  std::vector<History> history;
  std::vector<Item> items;
  std::vector<size_t> songIndices;
  int selected=0,scroll=0;
  enum class NowOverlay : uint8_t {Progress, Volume, Seek};
  NowOverlay nowOverlay=NowOverlay::Progress;
  uint32_t overlayUpdatedAt=0,lastWheelTurnAt=0;
  uint32_t lastNowTime=UINT32_MAX;
  int lastNowTrack=-1;
  int lastHiddenTrack=-1;
  int seekPreview=-1,lastSeekCommitted=-1;
  bool dirty=true,blank=false,brightnessEditing=false;
  uint32_t selectedSince=0,lastMarqueeFrame=UINT32_MAX,lastHiddenRefresh=0;
  uint32_t nowTitleSince=0,lastTitleFrame=UINT32_MAX;
  int marqueeSelection=-1;
  String marqueeTitle;
  int marqueeOffset(uint32_t since,uint32_t now)const;
  String selectedText()const;
  uint32_t lastTouch=0,lastRender=0,sleepDeadline=0;
  uint8_t lastWiFiIndicator=255,lastBatteryBand=255;
  // Network password keyboard. MENU toggles case; SELECT types; NEXT joins;
  // PREVIOUS deletes; Play/Pause cancels, Scroll moves selection.
  String wifiSSID,wifiPassword;
  bool wifiSelectedSecured=false;
  int keyboardIndex=0;
  bool keyboardLower=false;
  String deleteTrackPath, deleteTrackTitle;
  String toast;
  uint32_t toastDeadline=0;
#if FEATURE_BLUETOOTH
  uint32_t lastNetworkUiUpdate=0,btLastScan=0;
  uint8_t btAddress[6]={};
  String btDeviceName,btDigits;
  int btDigitIndex=0;
  bool btYes=false;
#endif
  void build();int listItemCount() const;void navigate(Page next);void back();void open();void paint();
  void adjust(int delta);bool listPage()const;String title()const;
  void chooseTrack(size_t idx,const std::vector<size_t>& q);
  void updateNowPlayingOverlay();
  void passwordEvent(Key key);void paintKeyboard();
#if FEATURE_BLUETOOTH
  void bluetoothEvent(Key key);void paintBluetoothDialog();
#endif
  void wifiConnect();void showMessage(const String& msg);
};
extern PlayerUI ui;
