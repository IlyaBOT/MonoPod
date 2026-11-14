#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <algorithm>
#include <random>
#include <vector>
#include "../DIY_iPod_Mono/FlacPictureSkipSource.h"

class FileSource final : public AudioFileSource {
public:
  explicit FileSource(const char* path) { fp=fopen(path,"rb"); assert(fp); assert(fseek(fp,0,SEEK_END)==0); length=ftell(fp);rewind(fp); }
  ~FileSource() override {if(fp)fclose(fp);}
  bool isOpen()override{return fp;}
  uint32_t getSize()override{return length;}
  uint32_t getPos()override{return (uint32_t)ftell(fp);}
  bool seek(int32_t pos,int dir)override{return fseek(fp,pos,dir)==0;}
  uint32_t read(void* b,uint32_t n)override {bytesRead+=n;return fread(b,1,n,fp);}
  bool close()override{return true;}
  uint64_t bytesRead=0;
private:FILE* fp=nullptr;uint32_t length=0;
};

int main(int argc,char** argv){
 assert(argc==4);
 FileSource original(argv[1]);
 FlacPictureSkipSource clean(&original);
 assert(clean.valid() && clean.skippedPictures()==1);
 assert(clean.skippedBytes()>9*1024*1024);
 assert(original.bytesRead<200); // large PICTURE is never read by the index pass
 FileSource noCover(argv[2]);
 assert(clean.getSize()==noCover.getSize()+4); // empty padding header replaces PICTURE
 uint8_t a[65536],b[65536];
 assert(clean.read(a,42)==42 && noCover.read(b,42)==42 && memcmp(a,b,42)==0);
 assert(clean.read(a,4)==4 && a[0]==1 && a[1]==0 && a[2]==0 && a[3]==0);
 assert(clean.read(a,4000)==4000 && noCover.read(b,4000)==4000 && memcmp(a,b,4000)==0);
 assert(clean.seek(0,SEEK_SET));
 FILE* output=fopen(argv[3],"wb");assert(output);
 uint32_t readBytes=0;
 while(readBytes<clean.getSize()){
   uint32_t n=clean.read(a,sizeof(a));assert(n>0);assert(fwrite(a,1,n,output)==n);readBytes+=n;
 }
 fclose(output);
 assert(clean.getPos()==clean.getSize());
 assert(clean.read(a,10)==0);
 std::mt19937 gen(0x55aabb);
 for(int i=0;i<500;i++){
   uint32_t p=gen()%clean.getSize();
   assert(clean.seek((int32_t)p,SEEK_SET));
   uint32_t n=std::min((uint32_t)sizeof(a),clean.getSize()-p);
   assert(clean.read(a,n)==n);
   FILE* file=fopen(argv[3],"rb");assert(file);assert(fseek(file,p,SEEK_SET)==0);
   assert(fread(b,1,n,file)==n);fclose(file);
   assert(memcmp(a,b,n)==0);
 }
 printf("FLAC picture skip native: %u MiB skipped; generated %u bytes; 500 random seeks OK\n", clean.skippedBytes()/(1024*1024),readBytes);
}
