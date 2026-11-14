#include "../DIY_iPod_Mono/Id3Parser.h"
#include <cstdio>
#include <cassert>
#include <cstring>
#include <string>
struct Reader {
  FILE* f;long n;
  Reader(const char* filename):f(fopen(filename,"rb")),n(0){if(f){fseek(f,0,SEEK_END);n=ftell(f);fseek(f,0,SEEK_SET);}}
  ~Reader(){if(f)fclose(f);}
  size_t size(){return size_t(n);}
  bool seek(unsigned long offset){return f && fseek(f,offset,SEEK_SET)==0;}
  int read(uint8_t* buf,size_t len){return f ? int(fread(buf,1,len,f)) : -1;}
};
int main(int argc,char** argv){
 assert(argc==2);
 Reader r(argv[1]);assert(r.f);
 Id3Parser::Tags t{};
 bool ok=Id3Parser::parse(r,t);
 printf("parsed=%d\ntitle=%s\nartist=%s\nalbum=%s\ngenre=%s\nyear=%u\nduration=%u\n",ok,t.title,t.artist,t.album,t.genre,t.year,t.durationSeconds);
 return !(ok && strcmp(t.title,"R4v3 B0y")==0 && strcmp(t.artist,"S3RL")==0 && strcmp(t.album,"R4v3 B0y")==0 && t.year==2015);
}
