#include "Display.h"
#include "MonoFont.h"
#include "Settings.h"
#include "Battery.h"
#include "Theme.h"
#include <pgmspace.h>
Display display;
Display::Display():tft(Config::PIN_TFT_CS,Config::PIN_TFT_DC,Config::PIN_TFT_RST){}
void Display::begin(){
#if WOKWI_SIMULATION
  tft.begin();tft.setRotation(0);tft.fillScreen(0);
#else
  tft.initR(INITR_BLACKTAB);tft.setRotation(Config::TFT_ROTATION);tft.setSPISpeed(16000000);
#endif
  palette(settings.data.contrast);clear();
}
bool Display::reloadTheme(){
  bool ok=theme.reload();palette(settings.data.contrast);return ok;
}
bool Display::themeLoaded() const {return theme.active();}
void Display::palette(uint8_t p){
  if(theme.active()){bg=theme.color(0);fg=theme.color(1);hi=theme.color(2);low=theme.color(3);return;}
  switch(p%4){
  case 0:bg=0xffff;fg=0;hi=0;low=0x7bef;break;
  case 1:bg=0xdefb;fg=0x1082;hi=0x2104;low=0x94b2;break;
  case 2:bg=0xce59;fg=0x2104;hi=0x2945;low=0x8c71;break;
  default:bg=0xbdf7;fg=0;hi=0x18a3;low=0x7390;break;
  }
}
void Display::clear(){tft.fillRect(OX,OY,160,128,bg);}
static bool utf8next(const String& s,size_t& i,uint32_t& cp){
  if(i>=s.length())return false;
  uint8_t c=(uint8_t)s[i++];if(c<128){cp=c;return true;}
  if((c&0xe0)==0xc0 && i<s.length()){
    cp=((uint32_t)(c&0x1f)<<6)|((uint8_t)s[i++]&0x3f);return true;
  }
  if((c&0xf0)==0xe0 && i+1<s.length()){
    uint8_t c2=(uint8_t)s[i++],c3=(uint8_t)s[i++];
    cp=((uint32_t)(c&0x0f)<<12)|((uint32_t)(c2&0x3f)<<6)|(c3&0x3f);return true;
  }
  cp='?';return true;
}
static bool fontFind(uint32_t cp,bool small,MonoFont::Glyph& out){
  const MonoFont::Glyph* a=small?MonoFont::small:MonoFont::big;
  const size_t n=small?MonoFont::smallCount:MonoFont::bigCount;
  for(size_t i=0;i<n;i++){
    if(pgm_read_word(&a[i].codepoint)==cp){memcpy_P(&out,&a[i],sizeof(out));return true;}
  }
  return false;
}
static uint16_t blend565(uint16_t a,uint16_t b,uint8_t opacity){
  const int inv=15-opacity;
  uint16_t r=((((a>>11)&31)*inv+((b>>11)&31)*opacity+7)/15);
  uint16_t g=((((a>>5)&63)*inv+((b>>5)&63)*opacity+7)/15);
  uint16_t bl=(((a&31)*inv+(b&31)*opacity+7)/15);
  return (r<<11)|(g<<5)|bl;
}
void Display::drawGlyph(int x,int y,uint32_t cp,uint16_t front,uint16_t back,bool small){
  ThemeManager::Glyph cg;
  if(theme.active() && (theme.glyph(cp,small,cg)||theme.glyph('?',small,cg))){
    const int height=small?12:16;
    for(int yy=0;yy<height;yy++){
      const int dy=y+yy;if(dy<0||dy>=128)continue;
      for(int xx=0;xx<cg.advance;xx++){
        const int dx=x+xx;if(dx<0||dx>=160)continue;
        uint8_t alpha= xx<cg.width?theme.alpha(cg,small,xx,yy):0;
        if(!settings.data.fontSmoothing)alpha=(alpha>=8)?15:0;
        tft.writePixel(OX+dx,OY+dy,alpha==15?front:alpha==0?back:blend565(back,front,alpha));
      }
    }
    return;
  }
  MonoFont::Glyph g; if(!fontFind(cp,small,g))fontFind('?',small,g);
  const int height=small?12:16;
  for(int yy=0;yy<height;yy++){
    const int dy=y+yy;if(dy<0||dy>=128)continue;
    uint32_t row=g.bits[yy];
    for(int xx=0;xx<g.advance;xx++){
      int dx=x+xx;
      if(dx>=0&&dx<160)tft.writePixel(OX+dx,OY+dy,(row&(0x80000000UL>>xx))?front:back);
    }
  }
}
static int glyphAdvance(uint32_t cp,bool small){
  ThemeManager::Glyph cg;
  if(theme.active()&&(theme.glyph(cp,small,cg)||theme.glyph('?',small,cg)))return cg.advance;
  MonoFont::Glyph g;if(!fontFind(cp,small,g))fontFind('?',small,g);return g.advance;
}
int Display::textWidth(const String& s,bool small)const{
  size_t i=0;uint32_t cp;int width=0;
  while(utf8next(s,i,cp))width+=glyphAdvance(cp,small);
  return width;
}
void Display::drawString(int x,int y,const String& value,uint16_t f,uint16_t b,bool small,uint8_t maxChars,int maxX){
  size_t i=0;uint32_t cp;uint8_t count=0;
  tft.startWrite();
  while(utf8next(value,i,cp) && (!maxChars || count<maxChars)){
    const int advance=glyphAdvance(cp,small);
    if(x+advance>maxX)break;
    drawGlyph(x,y,cp,f,b,small);x+=advance;count++;
  }
  tft.endWrite();
}
void Display::text(int x,int y,const String& s,uint16_t f,uint16_t b,uint8_t limit){drawString(x,y,s,f,b,false,limit);}
void Display::smallText(int x,int y,const String& s,uint16_t f,uint16_t b,uint8_t limit){drawString(x,y,s,f,b,true,limit);}
void Display::centerText(int y,const String& s,bool small,bool inv){
  const int w=textWidth(s,small);int x=max(2,(160-w)/2);
  drawString(x,y,s,inv?bg:fg,inv?hi:bg,small,0);
}
void Display::header(const String& title,bool held,bool playing,bool wifi){
  tft.fillRect(OX,OY,160,20,bg);
  // Reserve a permanent top-right area for power and connection status.
  drawString(5,2,title,fg,bg,false,0,113);
  if(held)smallText(117,3,"H",fg,bg);
  else if(wifi)smallText(117,3,"W",fg,bg);
  else if(playing)smallText(117,3,">",fg,bg);
  const int x=136,y=6;
  // 18x9 monochrome battery icon, plus a 2px terminal.
  tft.drawRect(OX+x,OY+y,19,9,fg);
  tft.fillRect(OX+x+19,OY+y+3,2,3,fg);
  int pct=battery.level();
  if(pct>=0){
    const int bars=(pct*15+50)/100;
    if(bars>0)tft.fillRect(OX+x+2,OY+y+2,bars,5,fg);
  } else {
    tft.fillRect(OX+x+8,OY+y+2,2,3,fg);
    tft.drawPixel(OX+x+8,OY+y+6,fg); // unknown voltage, not a fake charge estimate
  }
  tft.drawFastHLine(OX,OY+19,160,fg);
}
void Display::footer(const String& s){
  if(s.isEmpty())return;
  tft.drawFastHLine(OX,OY+114,160,fg);
  tft.fillRect(OX,OY+115,160,13,bg);
  smallText(5,115,s,fg,bg);
}
void Display::listRow(uint8_t row,const String& s,bool selected,bool submenu){
  if(row>=ROW_COUNT)return;
  int y=21+row*18;
  uint16_t back=selected?hi:bg,front=selected?bg:fg;
  tft.fillRect(OX+2,OY+y,156,18,back);
  text(6,y+1,s,front,back);
  if(submenu){tft.fillTriangle(OX+150,OY+y+6,OX+155,OY+y+9,OX+150,OY+y+12,front);}
}
void Display::progress(int x,int y,int w,int percent){
  percent=constrain(percent,0,100);
  tft.drawRect(OX+x,OY+y,w,8,fg);
  tft.fillRect(OX+x+2,OY+y+2,w-4,4,bg);
  if(percent) tft.fillRect(OX+x+2,OY+y+2,(w-4)*percent/100,4,fg);
}
void Display::syncScreen(bool wifi,bool transferring){
  clear();
  // Minimal 160x128 monochrome-like lock screen. No status bar/navigation.
  const int cx=80;
  if(wifi){
    // Three circular Wi-Fi arcs (show upper half only), plus center dot.
    for(int r=23;r>=9;r-=7)tft.drawCircle(OX+cx,OY+37,r,fg);
    tft.fillRect(OX+51,OY+38,58,30,bg);
    tft.fillCircle(OX+cx,OY+38,3,fg);
  }else{
    // USB trident icon: stem, square/circle/triangle tips.
    tft.drawLine(OX+cx,OY+41,OX+cx,OY+13,fg);
    tft.drawLine(OX+cx,OY+24,OX+65,OY+24,fg);
    tft.drawLine(OX+65,OY+24,OX+65,OY+17,fg);
    tft.fillRect(OX+62,OY+12,7,7,fg);
    tft.drawLine(OX+cx,OY+32,OX+95,OY+32,fg);
    tft.drawLine(OX+95,OY+32,OX+95,OY+23,fg);
    tft.fillCircle(OX+95,OY+22,4,fg);
    tft.fillTriangle(OX+cx,OY+5,OX+cx-5,OY+14,OX+cx+5,OY+14,fg);
    tft.fillCircle(OX+cx,OY+41,3,fg);
  }
  // Adafruit 5x7 bitmap scaled to ~18x24. Duplicate by 1px for boldness.
  tft.setTextSize(3);tft.setTextColor(fg);
  tft.setCursor(OX+44,OY+49);tft.print("SYNC");
  tft.setCursor(OX+45,OY+49);tft.print("SYNC");
  tft.setTextSize(1);
  const uint16_t color=fg,back=bg;
  smallText(3,82,transferring?"DATA TRANSFER ACTIVE":(wifi?"Wi-Fi sync active":"USB sync active"),color,back);
  smallText(3,96,"DO NOT REMOVE SD",color,back);
  smallText(3,110,wifi?"KEEP Wi-Fi CONNECTED":"DO NOT UNPLUG USB",color,back);
}
void Display::clock(const String& dt){centerText(47,dt,true);}
void Display::nowPlaying(const String& title,const String& artist,bool paused,int done,uint32_t seconds,uint8_t volume){
  centerText(27,title,false);centerText(51,artist,true);
  progress(13,78,134,done);
  smallText(12,93,String(seconds/60)+":"+((seconds%60<10)?"0":"")+String(seconds%60),fg,bg);
  smallText(95,93,String(paused?"Paused":"Playing")+" "+String(volume)+"%",fg,bg);
}
