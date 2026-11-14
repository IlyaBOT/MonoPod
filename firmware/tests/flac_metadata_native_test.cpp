#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include "../DIY_iPod_Mono/FlacMetadata.h"
class DiskFile {
public:
  explicit DiskFile(const std::string& name){std::ifstream in(name,std::ios::binary);data=std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());}
  explicit operator bool()const{return !data.empty();}
  size_t size()const{return data.size();}
  size_t position()const{return pos;}
  bool seek(uint32_t p){if(p>data.size())return false;pos=p;return true;}
  int read(uint8_t* out,size_t n){n=std::min(n,data.size()-pos);memcpy(out,data.data()+pos,n);pos+=n;return (int)n;}
private:std::vector<uint8_t>data;size_t pos=0;
};
int main(int argc,char**argv){
  assert(argc==2);
  DiskFile f(argv[1]);FlacMetadata::Info i;
  assert(FlacMetadata::parse(f,i));
  assert(i.sampleRate==44100 && i.totalSamples==88200 && i.durationSeconds==2);
  assert(!strcmp(i.title,"MonoPod Test") && !strcmp(i.artist,"Ilya"));
  assert(!strcmp(i.album,"MonoPod") && !strcmp(i.genre,"Electronic"));
  assert(i.year==2026);
  DiskFile g(argv[1]);g.seek(1);FlacMetadata::Info j;assert(FlacMetadata::parse(g,j));
  puts("FLAC STREAMINFO and Vorbis comments OK");
}
