#include <cstdint>
#include <cstddef>
#include <initializer_list>
#define PROGMEM
#define pgm_read_byte(ptr) (*(ptr))
#include "MonoFont.h"
#include <cassert>
int main(){
  for(bool small : {false,true}){
    const auto* g=small?MonoFont::small:MonoFont::big;
    const size_t n=small?MonoFont::smallCount:MonoFont::bigCount;
    assert(n>=160);
    uint32_t middle=0,black=0,white=0;
    for(size_t i=0;i<n;i++){
      for(uint8_t y=0;y<(small?12:16);y++){
        for(uint8_t x=0;x<g[i].width;x++){
          auto a=MonoFont::alpha(g[i],small,x,y);
          assert(a<=15);
          if(a==0)black++;else if(a==15)white++;else middle++;
        }
      }
    }
    assert(black>0 && white>0 && middle>0);
    assert(MonoFont::alpha(g[0],small,31,0)==0);
  }
}
