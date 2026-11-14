#pragma once
#include "Types.h"
#include <vector>
class PlayerUI {
public:
  void begin();
  void event(Key key);
  void loop();
private:
  struct Item {String title;int id;bool submenu;};
  struct History {Page page;int selection;int scroll;Category category;String filter;};
  Page page=Page::Home;
  Category activeCategory=Category::Artist;
  String filter;
  std::vector<History> history;
  std::vector<Item> items;
  std::vector<size_t> songIndices;
  int selected=0, scroll=0;
  bool dirty=true,blank=false;
  uint32_t lastTouch=0,lastRender=0;
  uint32_t sleepDeadline=0;
  void build();
  void navigate(Page next);
  void back();
  void open();
  void paint();
  void adjust(int delta);
  bool listPage() const;
  String title()const;
  void chooseTrack(size_t idx,const std::vector<size_t>& q);
};
extern PlayerUI ui;
