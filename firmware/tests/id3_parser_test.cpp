#include "../DIY_iPod_Mono/Id3Parser.h"
#include <vector>
#include <string>
#include <cassert>
#include <cstdio>
#include <cstring>
using Bytes=std::vector<uint8_t>;
struct FakeFile {
  Bytes d;size_t offset=0;
  explicit FakeFile(Bytes data):d(std::move(data)){}
  size_t size()const{return d.size();}
  bool seek(size_t n){if(n>d.size())return false;offset=n;return true;}
  size_t read(uint8_t* b,size_t n){n=n<d.size()-offset?n:d.size()-offset;memcpy(b,d.data()+offset,n);offset+=n;return n;}
};
static void u32(Bytes& v,uint32_t n){v.push_back(n>>24);v.push_back(n>>16);v.push_back(n>>8);v.push_back(n);}
static void sync(Bytes& v,uint32_t n){v.push_back((n>>21)&127);v.push_back((n>>14)&127);v.push_back((n>>7)&127);v.push_back(n&127);}
static void frame(Bytes& tag,const std::string& id,const Bytes& data,int ver,uint8_t format=0){
 for(char c:id)tag.push_back(c);
 if(ver==2){tag.push_back(data.size()>>16);tag.push_back(data.size()>>8);tag.push_back(data.size());}
 else{if(ver==4)sync(tag,data.size());else u32(tag,data.size());tag.push_back(0);tag.push_back(format);}
 tag.insert(tag.end(),data.begin(),data.end());
}
static Bytes text(const Bytes& s,uint8_t enc){Bytes b={enc};b.insert(b.end(),s.begin(),s.end());return b;}
static Bytes makeId3(int ver,const Bytes& frames,uint8_t flags=0){Bytes b={'I','D','3',(uint8_t)ver,0,flags};sync(b,frames.size());b.insert(b.end(),frames.begin(),frames.end());return b;}
static Bytes v1(const std::string& title,const std::string& artist,const std::string& album){Bytes b(128,0);memcpy(b.data(),"TAG",3);memcpy(b.data()+3,title.data(),std::min((size_t)30,title.size()));memcpy(b.data()+33,artist.data(),std::min((size_t)30,artist.size()));memcpy(b.data()+63,album.data(),std::min((size_t)30,album.size()));return b;}
static bool eq(const char* a,const char* b){if(strcmp(a,b)){printf("FAIL expected '%s' got '%s'\n",b,a);return false;}return true;}
int main(){
  {
    // ID3v2.3 title UTF-16LE BOM, artist UTF-16BE BOM, album latin1.
    Bytes frames;
    frame(frames,"TIT2",text({0xff,0xfe,0x1f,0x04,0x40,0x04,0x38,0x04,0x32,0x04,0x35,0x04,0x42,0x04,0x00,0x00},1),3);
    frame(frames,"TPE1",text({0xfe,0xff,0x04,0x26,0x04,0x3e,0x04,0x39,0x00,0x00},1),3);
    frame(frames,"TALB",text({'M',0xe9,'m','o','i','r','e'},0),3);
    frame(frames,"TLEN",text({'1','2','3','4','5','6'},0),3);
    FakeFile f(makeId3(3,frames));Id3Parser::Tags t;
    assert(Id3Parser::parse(f,t));
    assert(eq(t.title,u8"Привет"));assert(eq(t.artist,u8"Цой"));assert(eq(t.album,u8"Mémoire"));assert(t.durationSeconds==123);
  }
  {
    // ID3v2.4: per-frame unsynchronisation 0xFF 0x00 0xE0; UTF-8 preserving Unicode.
    Bytes frames;frame(frames,"TIT2",text({'M','u','s','i','c',0xff,0,0xe0,'A'},0),4,2);
    frame(frames,"TPE1",text({0xef,0xbb,0xbf,0xd0,0x90,0xd0,0xbb,0xd0,0xb8,0xd1,0x81,0xd0,0xb0},3),4);
    FakeFile f(makeId3(4,frames));Id3Parser::Tags t;assert(Id3Parser::parse(f,t));
    assert(eq(t.title,u8"MusicÿàA"));assert(eq(t.artist,u8"Алиса"));
  }
  {
    // ID3v2.2 old 3-char frames; CP1251 incorrectly advertised as Latin1.
    Bytes frames;frame(frames,"TT2",text({0xd2,0xe5,0xf1,0xf2},0),2);
    frame(frames,"TP1",text({'X'},0),2);
    FakeFile f(makeId3(2,frames));Id3Parser::Tags t;assert(Id3Parser::parse(f,t));
    assert(eq(t.title,u8"Тест"));assert(eq(t.artist,"X"));
  }
  {
    // v2.3 extended header: size 6 excluding the BE length word.
    Bytes frames;u32(frames,6);frames.insert(frames.end(),{0,0,0,0,0,0});
    frame(frames,"TIT2",text({'O','K'},3),3);
    FakeFile f(makeId3(3,frames,0x40));Id3Parser::Tags t;assert(Id3Parser::parse(f,t));assert(eq(t.title,"OK"));
  }
  {
    // ID3v1 fallback, no v2 tag.
    FakeFile f(v1("Song", "Artist", "Album"));Id3Parser::Tags t;assert(Id3Parser::parse(f,t));
    assert(eq(t.title,"Song"));assert(eq(t.artist,"Artist"));assert(eq(t.album,"Album"));
  }
  {
    // An APIC frame of 8KiB must never be loaded as text; scan continues.
    Bytes frames;frame(frames,"APIC",Bytes(8192,0x99),3);frame(frames,"TIT2",text({'O','K'},0),3);
    FakeFile f(makeId3(3,frames));Id3Parser::Tags t;assert(Id3Parser::parse(f,t));assert(eq(t.title,"OK"));
  }
  {
    // Bad frame size is rejected, rather than reading outside the input.
    Bytes frames={'T','I','T','2',0xff,0xff,0xff,0xff,0,0};
    FakeFile f(makeId3(3,frames));Id3Parser::Tags t;assert(!Id3Parser::parse(f,t));
  }
  {
    // UTF-16BE without BOM (encoding=2).
    Bytes frames;frame(frames,"TALB",text({0x04,0x12,0x04,0x35,0x04,0x41,0x04,0x3d,0x04,0x30},2),4);
    FakeFile f(makeId3(4,frames));Id3Parser::Tags t;assert(Id3Parser::parse(f,t));assert(eq(t.album,u8"Весна"));
  }
  {
    // Regression: cover art preceding ID3 text can exceed 256 KiB.
    Bytes frames;frame(frames,"APIC",Bytes(600*1024,0x91),3);
    frame(frames,"TIT2",text({'A','w','a','k','e',' ','A','n','d',' ','A','l','i','v','e'},3),3);
    frame(frames,"TPE1",text({'S','k','i','l','l','e','t'},3),3);
    frame(frames,"TALB",text({'i','T','u','n','e','s',' ','S','e','s','s','i','o','n',' ','(','E','P',')'},3),3);
    FakeFile f(makeId3(3,frames));Id3Parser::Tags t;
    assert(Id3Parser::parse(f,t));
    assert(eq(t.title,"Awake And Alive"));assert(eq(t.artist,"Skillet"));
    assert(eq(t.album,"iTunes Session (EP)"));
  }
  {
    // ID3v2.4 text with data length indicator (DLI) must be accepted.
    Bytes frames;Bytes payload;sync(payload,8);Bytes name=text({'S','k','i','l','l','e','t'},3);
    payload.insert(payload.end(),name.begin(),name.end());
    frame(frames,"TPE1",payload,4,0x01);
    FakeFile f(makeId3(4,frames));Id3Parser::Tags t;
    assert(Id3Parser::parse(f,t));assert(eq(t.artist,"Skillet"));
  }
  puts("ID3 parser tests: 10/10 PASS");
}
