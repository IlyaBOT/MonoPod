"""Compile the real Library.cpp with an isolated SD adapter; never opens a device."""
from pathlib import Path
import subprocess, shutil, json, tempfile, sys

P=Path(__file__).resolve().parents[1]
F=P.parent/'firmware'/'DIY_iPod_Mono'
T=P/'MonoPod.Desktop.Tests'/'native-library'
B=P/'work'/'native-library'
T.mkdir(parents=True,exist_ok=True);B.mkdir(parents=True,exist_ok=True)
def put(name,s): (T/name).write_text(s.strip()+'\n',encoding='utf-8')

put('Arduino.h',r'''
#pragma once
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <cctype>
using std::min;
class String:public std::string {
public:
using std::string::string;
String(const std::string& s):std::string(s){}
String(char c):std::string(1,c){}
String(int n):std::string(std::to_string(n)){}
String(unsigned n):std::string(std::to_string(n)){}
String(unsigned long n):std::string(std::to_string(n)){}
bool isEmpty()const{return empty();}
bool startsWith(const String& s)const{return rfind(s,0)==0;}
bool endsWith(const String& s)const{return size()>=s.size()&&compare(size()-s.size(),s.size(),s)==0;}
int indexOf(char c,int start=0)const{auto p=find(c,start);return p==npos?-1:(int)p;}
int lastIndexOf(char c)const{auto p=rfind(c);return p==npos?-1:(int)p;}
String substring(int from,int to=-1)const{if(from>(int)size())return "";return substr(from,to<0?npos:to-from);}
void remove(unsigned from){erase(from);}
void toLowerCase(){for(char& c:*this)c=(char)tolower((unsigned char)c);}
bool equalsIgnoreCase(String s)const{String a=*this;a.toLowerCase();s.toLowerCase();return a==s;}
int compareTo(const String& s)const{return compare(s);}
void replace(char a,char b){std::replace(begin(),end(),a,b);}
void trim(){auto a=find_first_not_of(" \t\r\n"),b=find_last_not_of(" \t\r\n");*this=a==npos?"":substr(a,b-a+1);}
};
inline void pinMode(int,int){} inline void digitalWrite(int,int){}
#define OUTPUT 1
#define HIGH 1
struct Log {template<class... T> void printf(const char*,T...){} template<class T> void println(T){} };
inline Log testLog;
#define MONOPOD_LOG testLog
''')
put('Config.h',r'''
#pragma once
#include "Arduino.h"
#define WOKWI_SIMULATION 1
#define FEATURE_SD_FORMAT 0
#define FEATURE_BLUETOOTH 0
namespace Config {constexpr size_t MAX_TRACKS=2048;constexpr uint8_t MAX_SCAN_DEPTH=6;constexpr int PIN_TFT_CS=10,PIN_SD_CS=14;constexpr uint32_t SPI_SD_HZ=4000000;}
''')
put('SPI.h','#pragma once\ninline int SPI=0;')
put('SD.h',r'''
#pragma once
#include "Arduino.h"
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>
#define FILE_READ 0
namespace fs=std::filesystem;
class File {
struct State {fs::path path;std::ifstream stream;bool directory=false,valid=false;std::vector<fs::path> entries;size_t cursor=0;};
std::shared_ptr<State> s;
public:
File()=default;
File(fs::path p):s(std::make_shared<State>()){
s->path=p;s->directory=fs::is_directory(p);if(s->directory){s->valid=true;for(auto& e:fs::directory_iterator(p))s->entries.push_back(e.path());}else{s->stream.open(p,std::ios::binary);s->valid=s->stream.is_open();}}
explicit operator bool()const{return s&&s->valid;}
bool isDirectory()const{return s&&s->directory;}
size_t size()const{return *this&&!s->directory?(size_t)fs::file_size(s->path):0;}
const char* name()const{static std::string n;n=s?s->path.filename().string():"";return n.c_str();}
File openNextFile(){return s&&s->cursor<s->entries.size()?File(s->entries[s->cursor++]):File();}
int read(uint8_t* b,size_t n){s->stream.read((char*)b,n);return (int)s->stream.gcount();}
int read(){return s->stream.get();}
bool seek(size_t n){s->stream.clear();s->stream.seekg(n);return s->stream.good();}
bool available(){return s&&s->stream.peek()!=EOF;}
String readStringUntil(char c){std::string line;std::getline(s->stream,line,c);return line;}
void close(){if(s){s->stream.close();s->valid=false;}}
};
struct SDAdapter {
fs::path root;
fs::path path(const char* p){return root/fs::path(p[0]=='/'?p+1:p);}
File open(const char* p,int=FILE_READ){return File(path(p));}
bool begin(int,int,uint32_t){return fs::is_directory(root);}
void end(){}
bool exists(const char* p){return fs::exists(path(p));}
bool mkdir(const char* p){return fs::create_directory(path(p));}
int cardType(){return 3;}
uint64_t cardSize(){return 64000000000ULL;}
uint64_t totalBytes(){return cardSize();}
uint64_t usedBytes(){return 1024;}
};
extern SDAdapter SD;
''')
put('library_test.cpp',r'''
#include "Library.h"
#include <iostream>
#include <stdexcept>
SDAdapter SD;
void check(bool b,const char* msg){if(!b)throw std::runtime_error(msg);}
void save(fs::path p,const std::vector<uint8_t>& b){fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);f.write((const char*)b.data(),b.size());}
void be32(std::vector<uint8_t>& b,size_t at,uint32_t n){for(int i=0;i<4;i++)b[at+i]=(uint8_t)(n>>(24-8*i));}
void le32(std::vector<uint8_t>& b,size_t at,uint32_t n){for(int i=0;i<4;i++)b[at+i]=(uint8_t)(n>>(8*i));}
int main(int argc,char**argv){try{
SD.root=argv[1];fs::path where=SD.root/"Music";
std::vector<uint8_t> mp3(4096);mp3[0]=255;mp3[1]=251;mp3[2]=144;mp3[3]=192;
memcpy(mp3.data()+21,"Xing",4);be32(mp3,25,1);be32(mp3,29,1000);
for(int depth=0;depth<=7;depth++){
 save(where/("track"+std::to_string(depth)+".mp3"),mp3);
 where/="nested";
}
std::vector<uint8_t> wav(44+88200);memcpy(wav.data(),"RIFF",4);memcpy(wav.data()+8,"WAVEfmt ",8);
le32(wav,28,88200);memcpy(wav.data()+36,"data",4);le32(wav,40,88200);save(SD.root/"Music"/"sample.wav",wav);
save(SD.root/"Music"/"empty.mp3",{});
check(media.begin(),"mount");
check(media.size()==9,"depth limit or track count");
int validMp3=0,waves=0,unknown=0;
for(const auto& t:media.all()){
 if(t.path.endsWith(".wav")){check(t.durationSeconds==1,"WAV duration");++waves;}
 else if(t.path.endsWith("empty.mp3")){check(t.durationSeconds==0,"unknown duration");++unknown;}
 else {check(t.durationSeconds==26,"Xing duration");++validMp3;}
 check(t.path.find("track7.mp3")==String::npos,"scanned beyond depth limit");
}
check(validMp3==7&&waves==1&&unknown==1,"case counts");
media.rescan();check(media.size()==9,"rescan duplicated files");
media.eject();check(!media.sdOK()&&media.size()==0,"eject");
std::cout<<"PASS actual Library.cpp: recursive SD directories through depth 6, reject depth 7, MP3 Xing, WAV, empty/unknown and rescan/eject\n";
return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
''')
for name in ['Library.cpp','Library.h','Types.h']:shutil.copy2(F/name,T/name)
bat=B/'compile.cmd'
bat.write_text(f'''@echo off
call "D:\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul
cl /nologo /O2 /EHsc /std:c++17 /utf-8 /I"{T}" "{T/'Library.cpp'}" "{T/'library_test.cpp'}" /Fe:"{B/'library_test.exe'}" /Fo:{B}/
''')
r=subprocess.run(['cmd','/c',str(bat)],capture_output=True,text=True,errors='replace')
(P/'docs'/'library-native-build.log').write_text(r.stdout+r.stderr,encoding='utf-8')
if r.returncode:print(r.stdout+r.stderr);sys.exit(r.returncode)
root=Path(tempfile.mkdtemp(prefix='sd-',dir=B))
r=subprocess.run([str(B/'library_test.exe'),str(root)],capture_output=True,text=True,errors='replace')
result={'exit_code':r.returncode,'output':r.stdout+r.stderr,'source_sha256':__import__('hashlib').sha256((F/'Library.cpp').read_bytes()).hexdigest(),'scope':'Actual Library.cpp under host SD adapter. Not an ESP32 or electrical SD test.'}
(P/'docs'/'library-native-tests.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print(r.stdout+r.stderr);sys.exit(r.returncode)
