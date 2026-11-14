#include "Display.h"
#include "PixelFont.h"
#include "Settings.h"
Display display;
Display::Display():tft(Config::PIN_TFT_CS,Config::PIN_TFT_DC,Config::PIN_TFT_RST){}
void Display::begin(){
#if WOKWI_SIMULATION
  tft.begin();
  tft.setRotation(0); // ILI9341 portrait: 240x320; logical 160x128 centered
  tft.fillScreen(0x0000);
#else
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(Config::TFT_ROTATION);
  tft.setSPISpeed(16000000); // conservative for jumper wires and shared SD bus
#endif
  palette(settings.data.contrast);clear();
}
void Display::palette(uint8_t p){
  switch(p%4){
  case 0:bg=0xFFFF;fg=0;hi=0x0000;low=0x7BEF;break;
  case 1:bg=0xDEFB;fg=0x1082;hi=0x2104;low=0x94B2;break;
  case 2:bg=0xCE59;fg=0x2104;hi=0x2945;low=0x8C71;break;
  case 3:bg=0xBDF7;fg=0x0000;hi=0x18A3;low=0x7390;break;
  }
}
void Display::clear(){tft.fillRect(OX,OY,160,128,bg);}
static bool nextCodepoint(const String &s,size_t &offset,uint32_t &cp){
  if(offset>=s.length())return false;
  uint8_t a=(uint8_t)s[offset++];
  if(a<0x80){cp=a;return true;}
  if((a&0xE0)==0xC0 && offset<s.length()){
    cp=((a&31)<<6)|((uint8_t)s[offset++]&63);return true;
  }
  if((a&0xF0)==0xE0 && offset+1<s.length()){
    const uint8_t b=(uint8_t)s[offset++];
    const uint8_t c=(uint8_t)s[offset++];
    cp=((a&15)<<12)|((b&63)<<6)|(c&63);return true;
  }
  cp='?';return true;
}
void Display::glyph(int x,int y,uint32_t cp,uint16_t f,uint16_t b){
  const int n=PixelFont::index(cp);
  for(int row=0;row<PixelFont::GLYPH_HEIGHT;row++){
    uint8_t bits=pgm_read_byte(PixelFont::glyphs+n*PixelFont::GLYPH_HEIGHT+row);
    for(int col=0;col<PixelFont::GLYPH_WIDTH;col++){
      int px=x+col,py=y+row;
      if(px>=0&&px<Config::SCREEN_W&&py>=0&&py<Config::SCREEN_H)
        tft.writePixel(px+OX,py+OY,(bits&(0x80>>col))?f:b);
    }
  }
}
void Display::text(int x,int y,String value,uint16_t f,uint16_t b,uint8_t maxChars){
  const int maxWidth=(Config::SCREEN_W-x)/PixelFont::GLYPH_WIDTH;
  const int maxAllowed=maxChars?min((int)maxChars,maxWidth):maxWidth;
  size_t k=0;int count=0;uint32_t cp;
  tft.startWrite();
  while(count<maxAllowed && nextCodepoint(value,k,cp)){
    glyph(x+count*PixelFont::GLYPH_WIDTH,y,cp,f,b);count++;
  }
  tft.endWrite();
}
void Display::header(const String &title,bool held,bool playing,bool wifi){
  tft.fillRect(OX,OY,160,17,bg);
  String s=title; if(s.length()>25)s.remove(25);
  text(4,2,s,fg,bg,13);
  if(playing)text(112,2,">",fg,bg,1);
  if(wifi)text(126,2,"W",fg,bg,1);
  if(held)text(136,2,"H",fg,bg,1);
  // No fake battery reading: add real ADC battery measurement in next hardware revision.
  tft.drawFastHLine(OX,OY+16,160,fg);
}
void Display::footer(const String &message){
  tft.drawFastHLine(OX,OY+113,160,fg);
  tft.fillRect(OX,OY+114,160,14,bg);
  text(5,116,message,fg,bg,18);
}
void Display::listRow(uint8_t row,const String &label,bool selected,bool submenu){
  if(row>=7)return;
  const int y=18+row*13;
  const uint16_t back=selected?hi:bg,front=selected?bg:fg;
  tft.fillRect(OX+1,OY+y,158,13,back);
  text(6,y+1,label,front,back,submenu?17:18);
  if(submenu) text(148,y+1,">",front,back,1);
}
void Display::progress(int x,int y,int w,int percent){
  tft.drawRect(OX+x,OY+y,w,7,fg);
  tft.fillRect(OX+x+2,OY+y+2,w-4,3,bg);
  if(percent>0)tft.fillRect(OX+x+2,OY+y+2,(w-4)*constrain(percent,0,100)/100,3,fg);
}
void Display::clock(const String& datetime){
  tft.fillRect(OX,OY+18,160,95,bg);
  text(4,38,"Clock",fg,bg);
  text(4,64,datetime,fg,bg,19);
}
void Display::nowPlaying(const String &title,const String &artist,bool paused,int percent,uint32_t seconds,uint8_t volume){
  tft.fillRect(OX,OY+18,160,95,bg);
  text(5,29,title,fg,bg,18);
  text(5,49,artist,low,bg,18);
  progress(6,74,148,percent);
  char tm[15];snprintf(tm,sizeof tm,"%02lu:%02lu",(unsigned long)(seconds/60),(unsigned long)(seconds%60));
  text(6,86,tm,fg,bg);
  text(108,86,paused?"PAUSE":"PLAY",fg,bg);
  String v="VOL "+String(volume)+"%";
  text(5,101,v,low,bg,18);
}
