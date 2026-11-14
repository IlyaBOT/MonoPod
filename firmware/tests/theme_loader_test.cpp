#include "Theme.h"
#include "SD.h"
#include <iostream>
SDAdapter SD;
int main(int argc,char **argv){
  if(argc!=2)return 10;
  if(!theme.reload(argv[1]))return 1;
  ThemeManager::Glyph g;
  if(!theme.glyph('M',false,g)||g.width==0||theme.alpha(g,false,0,0)>15)return 2;
  if(!theme.glyph(0x416,false,g))return 3; // Cyrillic Zh
  std::cout<<"theme parsed; M, Ж present; 4-bit coverage ok\n";
}
