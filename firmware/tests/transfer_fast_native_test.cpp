#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstring>
#include "../DIY_iPod_Mono/FastCrc32.h"
#include "../DIY_iPod_Mono/BufferedSdWrite.h"

static uint32_t slowStep(uint32_t crc, const uint8_t* data, size_t n) {
 for (size_t i=0;i<n;i++) {
  crc ^= data[i];
  for (int j=0;j<8;j++) crc=(crc>>1)^((crc&1)?0xEDB88320UL:0UL);
 }
 return crc;
}
struct MockWriter {
 std::vector<uint8_t> output;
 size_t calls=0,limit=999999;
 bool fail=false;
 size_t write(const uint8_t* p,size_t n) {
  ++calls;
  if (fail) return 0;
  size_t take=n<limit?n:limit;
  output.insert(output.end(),p,p+take);
  return take;
 }
};
int main(){
 const char* sample="123456789";
 assert(~FastCrc32::update(~uint32_t(0),(const uint8_t*)sample,9)==0xCBF43926UL);
 std::vector<uint8_t> stream(1024*1024+137);
 uint32_t rand=0x12345678u;
 for(auto& b:stream){rand=rand*1664525u+1013904223u;b=(uint8_t)(rand>>17);}
 uint32_t slow=slowStep(~uint32_t(0),stream.data(),stream.size());
 uint32_t fast=~uint32_t(0);
 for(size_t off=0;off<stream.size();) {size_t n=1+(off%1500);if(n>stream.size()-off)n=stream.size()-off;fast=FastCrc32::update(fast,stream.data()+off,n);off+=n;}
 assert(fast==slow);
 assert(~FastCrc32::update(~uint32_t(0),stream.data(),stream.size())==~slow);
 printf("PASS: CRC32 IEEE known vector, random 1MiB, incremental chunks\n");
 uint8_t b[16384];
 BufferedSdWrite::Buffer buffer;buffer.reset(b,sizeof(b));
 MockWriter writer;
 auto consume=[&](const uint8_t* p,size_t n){return writer.write(p,n);};
 size_t off=0;
 while(off<stream.size()){
  size_t n=1+(off%1436);if(n>stream.size()-off)n=stream.size()-off;
  assert(buffer.append(stream.data()+off,n,consume));off+=n;
 }
 assert(buffer.flush(consume));
 assert(writer.output==stream);
 assert(writer.calls<100);
 printf("PASS: 1436-byte network chunks aggregated into <=100 SD writes\n");
 buffer.reset(b,sizeof(b));writer=MockWriter();writer.limit=900;
 assert(buffer.append(stream.data(),32768,consume));
 assert(buffer.append(stream.data()+32768,700,consume));
 assert(buffer.flush(consume));
 assert(writer.output.size()==32768+700);
 assert(memcmp(writer.output.data(),stream.data(),writer.output.size())==0);
 printf("PASS: direct write > buffer and partial SD writes\n");
 buffer.reset(b,sizeof(b));writer=MockWriter();
 assert(buffer.append(stream.data(),6000,consume));
 // Cancelation intentionally does not flush; only complete chunks are written.
 assert(writer.output.empty());buffer.reset(nullptr,0);
 printf("PASS: canceled pending writes are discarded\n");
 buffer.reset(b,sizeof(b));writer=MockWriter();writer.fail=true;
 assert(buffer.append(stream.data(),8192,consume));
 assert(!buffer.flush(consume));
 printf("PASS: write failure detected and surfaces to abort caller\n");
 buffer.reset(nullptr,0);writer=MockWriter();
 assert(buffer.append(stream.data(),100,consume));
 assert(writer.output.size()==100);
 printf("PASS: allocation failure falls back to direct write\n");
}
