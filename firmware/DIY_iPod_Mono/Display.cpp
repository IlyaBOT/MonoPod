#include "Display.h"
#include "BavPixel.h"
#include "MonoFont.h"
#include "BootLogoBitmap.h"
#include "Settings.h"
#include "Battery.h"
#include "Theme.h"
#include "MarqueeMotion.h"
#include <pgmspace.h>
#include <SD.h>
Display display;
Display::Display():tft(Config::PIN_TFT_CS,Config::PIN_TFT_DC,Config::PIN_TFT_RST){}
void Display::begin(){
  tft.initR(INITR_BLACKTAB);tft.setRotation(Config::TFT_ROTATION);tft.setSPISpeed(Config::SPI_TFT_HZ);
  palette(settings.data.contrast);clear();
#if MONOPOD_BOARD == MONOPOD_BOARD_ESP32_S3
  buffer=new GFXcanvas16(160,128);
  if(!buffer || !buffer->getBuffer()){
    if(buffer){delete buffer;buffer=nullptr;}
    MONOPOD_LOG.println("TFT: framebuffer unavailable; using direct drawing");
  }else MONOPOD_LOG.println("TFT: 160x128 framebuffer enabled");
#endif
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
uint16_t Display::themed(uint8_t i,uint16_t fallback)const{return theme.active()?theme.color(i):fallback;}
void Display::clear(){surface().fillRect(OX,OY,160,128,bg);}
void Display::startFrame(){inFrame=(buffer && buffer->getBuffer());}
void Display::endFrame(){
  if(!inFrame)return;
  inFrame=false;
  // Dirty sections of Now Playing are updated directly, not via full-frame DMA.
  tft.drawRGBBitmap(OX,OY,buffer->getBuffer(),160,128);
}
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
void Display::drawGlyph(int x,int y,uint32_t cp,uint16_t front,uint16_t back,bool small,int clipLeft,int clipRight){
  ThemeManager::Glyph cg;
  if(theme.active() && (theme.glyph(cp,small,cg)||theme.glyph('?',small,cg))){
    const int height=small?12:16;
    for(int yy=0;yy<height;yy++){
      const int dy=y+yy;if(dy<0||dy>=128)continue;
      for(int xx=0;xx<cg.advance;xx++){
        const int dx=x+xx;if(dx<clipLeft||dx>=clipRight||dx<0||dx>=160)continue;
        uint8_t alpha= xx<cg.width?theme.alpha(cg,small,xx,yy):0;
        if(!settings.data.fontSmoothing)alpha=(alpha>=8)?15:0;
        surface().writePixel(OX+dx,OY+dy,alpha==15?front:alpha==0?back:blend565(back,front,alpha));
      }
    }
    return;
  }
  MonoFont::Glyph g; if(!fontFind(cp,small,g))fontFind('?',small,g);
  const int height=small?12:16;
  for(int yy=0;yy<height;yy++){
    const int dy=y+yy;if(dy<0||dy>=128)continue;
    for(int xx=0;xx<g.advance;xx++){
      const int dx=x+xx;
      if(dx<clipLeft||dx>=clipRight||dx<0||dx>=160)continue;
      uint8_t alpha=MonoFont::alpha(g,small,xx,yy);
      if(!settings.data.fontSmoothing)alpha=(alpha>=8)?15:0;
      surface().writePixel(OX+dx,OY+dy,
        alpha==15?front:(alpha==0?back:blend565(back,front,alpha)));
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
  surface().startWrite();
  while(utf8next(value,i,cp) && (!maxChars || count<maxChars)){
    const int advance=glyphAdvance(cp,small);
    if(x+advance>maxX)break;
    drawGlyph(x,y,cp,f,b,small);x+=advance;count++;
  }
  surface().endWrite();
}
void Display::text(int x,int y,const String& s,uint16_t f,uint16_t b,uint8_t limit){drawString(x,y,s,f,b,false,limit);}
void Display::smallText(int x,int y,const String& s,uint16_t f,uint16_t b,uint8_t limit){drawString(x,y,s,f,b,true,limit);}
void Display::serialText(int x,int y,const String& serial,uint16_t front,uint16_t back){
  // Built-in 5x7 font has an exact 6px advance, compared to 8-10px for
  // the regular 12px small font. A 16-char GUID takes 96px, so there is no
  // cropping or scrolling and both S/N and the entire value fit on one row.
  // The 2px y-offset visually aligns this 8px face with the 12px label.
  surface().setTextSize(1);
  surface().setTextWrap(false);
  surface().setTextColor(front,back);
  surface().setCursor(OX+x,OY+y+2);
  surface().print(serial);
}
void Display::centerText(int y,const String& s,bool small,bool inv){
  const int w=textWidth(s,small);int x=max(2,(160-w)/2);
  drawString(x,y,s,inv?bg:fg,inv?hi:bg,small,0);
}
void Display::wifiSignal(uint8_t bars){
  // The left side is reserved for network/hold, leaving the header truly centered.
  surface().fillRect(OX+2,OY,17,19,bg);
  if(bars==0)return;
  if(bars>3)bars=3;
  for(uint8_t i=0;i<bars;i++){
    const int height=3+3*i;
    surface().fillRect(OX+3+5*i,OY+15-height,3,height,themed(9,fg));
  }
}
String Display::fittedTitle(const String& title, int width,bool small) const {
  if(textWidth(title,small)<=width)return title;
  const String dots="...";
  const int dotsWidth=textWidth(dots,small);
  String result;
  // Iterate by UTF-8 codepoint, never cut a multibyte glyph in half.
  size_t i=0;uint32_t cp=0;
  while(i<title.length()){
    size_t before=i;
    if(!utf8next(title,i,cp))break;
    const String prefix=title.substring(0,i);
    if(textWidth(prefix,small)+dotsWidth>width){i=before;break;}
    result=prefix;
  }
  return result+dots;
}
void Display::header(const String& title,bool held,bool playbackActive,uint8_t wifiBars,bool nowPlaying,bool playbackPaused){
  surface().fillRect(OX,OY,160,20,bg);
  // Show current playback status in *every* menu; while idle use Wi-Fi indicator.
  // HOLD overrides the playback glyph; never show PAUSE for an inactive player.
  if(held || playbackActive || nowPlaying){
    surface().fillRect(OX+2,OY,30,19,bg);
    if(held)smallText(20,5,"H",fg,bg);
    else if(playbackActive && playbackPaused){
      surface().fillRect(OX+7,OY+4,3,11,themed(7,fg));
      surface().fillRect(OX+14,OY+4,3,11,themed(7,fg));
    }else if(playbackActive){
      surface().fillTriangle(OX+7,OY+4,OX+7,OY+15,OX+17,OY+9,themed(7,fg));
    }
  }else{
    wifiSignal(wifiBars);
  }
  headerTitleMarquee(title,0);
  batteryIcon();
  surface().drawFastHLine(OX,OY+19,160,themed(11,fg));
}
void Display::footer(const String& s){
  if(s.isEmpty())return;
  surface().drawFastHLine(OX,OY+114,160,themed(11,fg));
  surface().fillRect(OX,OY+115,160,13,bg);
  smallText(5,115,s,fg,bg);
}
// A pixel-clipped marquee: no glyph can overlap a neighboring row or icon.
// Repeated text begins arriving from the right before the previous copy exits.
void Display::windowText(int left,int y,int width,const String& value,bool small,int offset,uint16_t front,uint16_t back){
  if(width<=0 || left<0 || left+width>160)return;
  const int h=small?12:16;
  surface().fillRect(OX+left,OY+y,width,h,back);
  const int wordWidth=textWidth(value,small);
  if(wordWidth<=0)return;
  if(wordWidth<=width){
    surface().startWrite();
    int x=left+(width-wordWidth)/2;
    size_t i=0;uint32_t cp;
    while(utf8next(value,i,cp)){
      drawGlyph(x,y,cp,front,back,small,left,left+width);x+=glyphAdvance(cp,small);
    }
    surface().endWrite();
    return;
  }
  const int period=wordWidth+MarqueeMotion::kGapPixels;
  int pos=MarqueeMotion::phase((uint32_t)offset,wordWidth);
  if(pos<0)pos+=period;
  // Skip off-window glyphs without allocating clipped temporary strings.
  surface().startWrite();
  for(int copy=0;copy<2;copy++){
    int x=left-pos+copy*period;
    size_t i=0;uint32_t cp;
    while(utf8next(value,i,cp)){
      const int advance=glyphAdvance(cp,small);
      if(x<left+width && x+advance>left)
        drawGlyph(x,y,cp,front,back,small,left,left+width);
      x+=advance;
      if(x>=left+width)break;
    }
  }
  surface().endWrite();
}
void Display::headerTitleMarquee(const String& label,int scrollPx){
  constexpr int left=34,width=100;
  const bool small=textWidth(label,false)>width;
  // Clear the title rectangle only. The left control icon and battery persist.
  surface().fillRect(OX+left,OY+2,width,17,bg);
  windowText(left,small?4:3,width,label,small,scrollPx,themed(8,fg),bg);
}
void Display::listRow(uint8_t row,const String& s,bool selected,bool submenu,int scrollPx){
  if(row>=ROW_COUNT)return;
  int y=21+row*18;
  uint16_t back=selected?hi:bg,front=selected?themed(4,bg):fg;
  surface().fillRect(OX+2,OY+y,156,18,back);
  const int maxWidth=submenu?137:149;
  if(selected && textWidth(s)>maxWidth)
    windowText(6,y+1,maxWidth,s,false,scrollPx,front,back);
  else
    text(6,y+1,textWidth(s)>maxWidth?fittedTitle(s,maxWidth):s,front,back);
  if(submenu){surface().fillTriangle(OX+150,OY+y+6,OX+155,OY+y+9,OX+150,OY+y+12,themed(selected?6:5,front));}
}
void Display::progress(int x,int y,int w,int percent){
  percent=constrain(percent,0,100);
  surface().drawRect(OX+x,OY+y,w,8,themed(11,fg));
  surface().fillRect(OX+x+2,OY+y+2,w-4,4,bg);
  if(percent) surface().fillRect(OX+x+2,OY+y+2,(w-4)*percent/100,4,themed(10,fg));
}
void Display::syncScreen(bool wifi,bool transferring,const String& address){
  clear();
  // Minimal 160x128 monochrome-like lock screen. No status bar/navigation.
  const int cx=80;
  if(wifi){
    // Three circular Wi-Fi arcs (show upper half only), plus center dot.
    for(int r=23;r>=9;r-=7)surface().drawCircle(OX+cx,OY+37,r,themed(5,fg));
    surface().fillRect(OX+51,OY+38,58,30,bg);
    surface().fillCircle(OX+cx,OY+38,3,themed(5,fg));
  }else{
    // USB trident icon: stem, square/circle/triangle tips.
    surface().drawLine(OX+cx,OY+41,OX+cx,OY+13,themed(5,fg));
    surface().drawLine(OX+cx,OY+24,OX+65,OY+24,themed(5,fg));
    surface().drawLine(OX+65,OY+24,OX+65,OY+17,themed(5,fg));
    surface().fillRect(OX+62,OY+12,7,7,themed(5,fg));
    surface().drawLine(OX+cx,OY+32,OX+95,OY+32,themed(5,fg));
    surface().drawLine(OX+95,OY+32,OX+95,OY+23,themed(5,fg));
    surface().fillCircle(OX+95,OY+22,4,themed(5,fg));
    surface().fillTriangle(OX+cx,OY+5,OX+cx-5,OY+14,OX+cx+5,OY+14,themed(5,fg));
    surface().fillCircle(OX+cx,OY+41,3,themed(5,fg));
  }
  // Adafruit 5x7 bitmap scaled to ~18x24. Duplicate by 1px for boldness.
  surface().setTextSize(3);surface().setTextColor(themed(7,fg));
  surface().setCursor(OX+44,OY+49);surface().print("SYNC");
  surface().setCursor(OX+45,OY+49);surface().print("SYNC");
  surface().setTextSize(1);
  const uint16_t color=fg,back=bg;
  smallText(3,77,transferring?"DATA TRANSFER ACTIVE":(wifi?"Wi-Fi sync active":"USB sync active"),color,back);
  if(wifi && !address.isEmpty())smallText(3,91,"IP: "+address,color,back);
  smallText(3,101,"DO NOT REMOVE SD",color,back);
  smallText(3,113,wifi?"KEEP Wi-Fi CONNECTED":"DO NOT UNPLUG USB",color,back);
}
void Display::clock(const String& dt){centerText(47,dt,true);}
static String timeLabel(uint32_t seconds){
  return String(seconds/60)+":"+(seconds%60<10?"0":"")+String(seconds%60);
}
uint8_t Display::batteryBand()const {
  int pct=battery.level();
  if(pct<0)return 255;
  if(pct>=75)return 4;
  if(pct>=50)return 3;
  if(pct>=25)return 2;
  if(pct>0)return 1;
  return 0;
}
void Display::batteryIcon(){
  const int bx=136,by=6;
  surface().fillRect(OX+bx,OY+by,22,10,bg);
  surface().drawRect(OX+bx,OY+by,19,9,themed(9,fg));
  surface().fillRect(OX+bx+19,OY+by+3,2,3,themed(9,fg));
  int b=batteryBand();
  if(b==255){
    surface().fillRect(OX+bx+8,OY+by+2,2,3,themed(9,fg));
    surface().drawPixel(OX+bx+8,OY+by+6,themed(9,fg));
  }else if(b){
    surface().fillRect(OX+bx+2,OY+by+2,(15*b)/4,5,themed(9,fg));
  }
}
void Display::nowPlaying(const String& title,const String& artist,const String& album,bool paused,uint32_t trackNumber,uint32_t trackCount){
  surface().fillRect(OX,OY+20,160,108,bg);
  smallText(7,21,String(trackNumber)+" of "+String(trackCount),fg,bg);
  // A dedicated 12px metadata face preserves the original iPod layout.
  nowPlayingTitleMarquee(title,0);
  centerText(56,fittedTitle(artist,152,true),true);
  centerText(74,fittedTitle(album,152,true),true);
  (void)paused;
}
void Display::nowPlayingTitleMarquee(const String& title,int scrollPx){
  windowText(4,38,152,title,true,scrollPx,fg,bg);
}
void Display::nowPlayingControls(uint8_t mode,uint8_t progress,uint8_t volume,uint8_t scrub,uint32_t elapsed,uint32_t remaining,bool knownDuration){
  // Update only the bottom region; never erase the header and metadata lines.
  surface().fillRect(OX,OY+96,160,32,bg);
  surface().drawRoundRect(OX+13,OY+96,134,10,5,themed(11,fg));
  const int percent=mode==1?volume:mode==2?scrub:progress;
  if(mode==2){
    int px=16+125*constrain(percent,0,100)/100;
    surface().fillTriangle(OX+px,OY+95,OX+px-4,OY+100,OX+px+4,OY+100,fg);
    surface().fillTriangle(OX+px,OY+107,OX+px-4,OY+102,OX+px+4,OY+102,fg);
    if(knownDuration){uint32_t total=elapsed+remaining,at=(uint32_t)((uint64_t)total*scrub/100ULL);
      smallText(6,112,timeLabel(at),fg,bg);
      String right="-"+timeLabel(total-at);smallText(154-textWidth(right,true),112,right,fg,bg);
    }
  }else if(mode==1){
    surface().fillRect(OX+15,OY+99,129*constrain(percent,0,100)/100,4,themed(7,fg));
    // Small / big speaker pictograms as on the iPod volume overlay.
    surface().fillRect(OX+10,OY+114,3,6,fg);
    surface().fillTriangle(OX+13,OY+113,OX+17,OY+110,OX+17,OY+123,fg);
    surface().fillRect(OX+140,OY+114,3,6,fg);
    surface().fillTriangle(OX+143,OY+113,OX+148,OY+110,OX+148,OY+123,fg);
    smallText(69,112,String(volume)+"%",fg,bg);
  }else{
    surface().fillRoundRect(OX+15,OY+98,129*constrain(percent,0,100)/100,6,3,themed(10,fg));
    smallText(6,112,timeLabel(elapsed),fg,bg);
    String right=knownDuration?"-"+timeLabel(remaining):"--:--";
    const int w=textWidth(right,true);
    smallText(154-w,112,right,fg,bg);
  }
}
// RGB565 boot asset: /MonoPod/boot.rgb565, 160*128*2 bytes, little-endian.
void Display::bootLogo(){
  tft.fillRect(OX,OY,160,128,0x0000);
  File f=SD.open("/MonoPod/boot.rgb565",FILE_READ);
  if(f && f.size()==160UL*128UL*2UL){
    uint16_t line[160];
    for(int y=0;y<128;y++){
      if(f.read((uint8_t*)line,sizeof(line))!=sizeof(line))break;
      tft.drawRGBBitmap(OX,OY+y,line,160,1);
    }
    f.close();return;
  }
  if(f)f.close();
  // Old iPod-style white silhouette on black; a theme RGB565 image overrides it.
  tft.drawBitmap(OX+(160-appleBootW)/2,OY+(128-appleBootH)/2,appleBootBitmap,appleBootW,appleBootH,0xffff);
}
bool Display::streamMonoFrame(const uint8_t* bits,uint16_t white,uint16_t black){
  if(!bits)return false;
  uint16_t row[160];
  tft.startWrite();tft.setAddrWindow(OX,OY,160,128);
  for(int y=0;y<128;y++){
    for(int x=0;x<160;x++){
      int i=y*160+x;row[x]=(bits[i>>3]&(0x80>>(i&7)))?white:black;
    }
    tft.writePixels(row,160);
  }
  tft.endWrite();return true;
}

bool Display::streamGray2Frame(const uint8_t* bits,const uint16_t colors[4]){
  if(!bits)return false;
  uint16_t row[160];
  tft.startWrite();tft.setAddrWindow(OX,OY,160,128);
  for(int y=0;y<128;y++){
    for(int x=0;x<160;x++){
      const int i=y*160+x;
      const uint8_t v=(bits[i>>2]>>(6-2*(i&3)))&3;
      row[x]=colors[v];
    }
    tft.writePixels(row,160);
  }
  tft.endWrite();return true;
}

// One SPI transaction per complete frame, line conversion without a 40KB stack array.
bool Display::streamGray6Frame(const uint8_t* bits){
  if(!bits)return false;
  uint16_t row[160];
  tft.startWrite();tft.setAddrWindow(OX,OY,160,128);
  for(int y=0;y<128;y++){
    for(int x=0;x<160;x++){
      const uint32_t pixel=(uint32_t)y*160u+(uint32_t)x;
      row[x]=BavPixel::gray6To565(BavPixel::gray6(bits,pixel));
    }
    tft.writePixels(row,160);
  }
  tft.endWrite();return true;
}
bool Display::streamRgb565Frame(const uint8_t* bits){
  if(!bits)return false;
  uint16_t row[160];
  tft.startWrite();tft.setAddrWindow(OX,OY,160,128);
  for(int y=0;y<128;y++){
    const uint8_t* src=bits+(uint32_t)y*160u*2u;
    for(int x=0;x<160;x++)row[x]=BavPixel::rgb565(src,(size_t)x);
    tft.writePixels(row,160);
  }
  tft.endWrite();return true;
}
