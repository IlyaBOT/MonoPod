// Resample a BAV3 delta video while preserving the original MP3 audio verbatim.
// Build: c++ -O3 -std=c++17 tools/resample_bav3.cpp -o resample_bav3
// Run: ./resample_bav3 old.bav new.bav 15
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace std;
static uint16_t u16(const uint8_t* p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
static uint32_t u32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
static void put16(uint8_t* p,uint16_t v){p[0]=uint8_t(v);p[1]=uint8_t(v>>8);}
static void put32(uint8_t* p,uint32_t v){for(int i=0;i<4;i++)p[i]=uint8_t(v>>(i*8));}
static void must(bool ok,const char* why){if(!ok)throw runtime_error(why);}
static void readExact(ifstream& f,uint8_t* p,size_t n){f.read((char*)p,n);must(f.gcount()==(streamsize)n,"truncated BAV");}
static vector<uint8_t> encodeDelta(const vector<uint8_t>& cur,const vector<uint8_t>& prev){
 vector<uint8_t> p; p.reserve(cur.size()+cur.size()/128);
 for(size_t i=0;i<cur.size();){
  const bool zero=(cur[i]==prev[i]); size_t j=i+1;
  while(j<cur.size() && j-i<128 && (cur[j]==prev[j])==zero)++j;
  p.push_back(uint8_t((zero?0x80:0)|(j-i-1)));
  if(!zero)for(size_t k=i;k<j;k++)p.push_back(cur[k]^prev[k]);
  i=j;
 }
 return p;
}
static void decodePacket(vector<uint8_t>& pixels,const vector<uint8_t>& pkt){
 size_t i=0,pos=0;
 while(i<pkt.size()){
  uint8_t c=pkt[i++]; size_t n=(c&127)+1;
  must(pos+n<=pixels.size(),"frame overflows pixel buffer");
  if(!(c&128)){must(i+n<=pkt.size(),"truncated literal");for(size_t j=0;j<n;j++)pixels[pos+j]^=pkt[i+j];i+=n;}
  pos+=n;
 }
 must(pos==pixels.size(),"incomplete frame packet");
}
int main(int argc,char**argv){
 try{
  must(argc==4,"usage: resample_bav3 input.bav output.bav target_fps");
  int destFps=stoi(argv[3]);must(destFps>0&&destFps<=120,"invalid target FPS");
  ifstream in(argv[1],ios::binary);must((bool)in,"cannot open input");
  uint8_t hdr[25];readExact(in,hdr,25);
  must(string((char*)hdr,4)=="BAV3","requires BAV3");
  const uint16_t w=u16(hdr+4),h=u16(hdr+6),srcFps=u16(hdr+8);
  const uint32_t n=u32(hdr+10),audioOff=u32(hdr+14),audioSize=u32(hdr+18);
  const uint8_t codec=hdr[22],fmt=hdr[23];
  must(w==160&&h==128&&srcFps>0&&n>0&&audioOff>=25&&fmt>=1&&fmt<=2,"unsupported BAV3 header");
  must(codec==0||codec==1,"unsupported BAV audio codec");
  const size_t bytes=fmt==1?15360:40960;
  const uint32_t outFrames=uint32_t(((uint64_t)(n-1)*destFps+srcFps/2)/srcFps+1);
  ofstream out(argv[2],ios::binary);must((bool)out,"cannot open output");
  put16(hdr+8,(uint16_t)destFps);put32(hdr+10,outFrames);put32(hdr+14,0);
  out.write((char*)hdr,25);
  vector<uint8_t> pixels(bytes,0),prev(bytes,0);
  uint32_t readFrames=0;
  for(uint32_t j=0;j<outFrames;j++){
   uint32_t srcIdx=uint32_t(((uint64_t)j*srcFps+destFps/2)/destFps);
   srcIdx=min(srcIdx,n-1);
   while(readFrames<=srcIdx){
    uint8_t lenb[2];readExact(in,lenb,2);uint16_t len=u16(lenb);must(len>0,"invalid packet length");
    vector<uint8_t> packet(len);readExact(in,packet.data(),len);decodePacket(pixels,packet);readFrames++;
   }
   vector<uint8_t> pkt=encodeDelta(pixels,prev);must(pkt.size()>0&&pkt.size()<=65535,"output packet size overflow");
   uint8_t lenb[2];put16(lenb,(uint16_t)pkt.size());out.write((char*)lenb,2);out.write((char*)pkt.data(),pkt.size());
   prev=pixels;
  }
  // Validate and consume the remaining video frames before copying audio.
  while(readFrames<n){uint8_t b[2];readExact(in,b,2);auto len=u16(b);must(len>0,"invalid tail frame");in.seekg(len,ios::cur);readFrames++;}
  must((uint64_t)in.tellg()==audioOff,"BAV video/audio boundary mismatch");
  uint32_t outAudioOff=(uint32_t)out.tellp();
  vector<char> buf(65536);uint32_t left=audioSize;
  while(left){auto size=min((uint32_t)buf.size(),left);in.read(buf.data(),size);must((uint32_t)in.gcount()==size,"truncated audio");out.write(buf.data(),size);left-=size;}
  put32(hdr+14,outAudioOff);out.seekp(0);out.write((char*)hdr,25);out.close();
  cout<<"BAV3 "<<srcFps<<" -> "<<destFps<<" fps, "<<n<<" -> "<<outFrames<<" frames; MP3 bytes preserved="<<audioSize<<"\n";
 }catch(const exception& e){cerr<<"ERROR: "<<e.what()<<"\n";return 1;}
}
