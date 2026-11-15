from pathlib import Path
import subprocess,shutil,sys,json,struct,zlib
P=Path(__file__).resolve().parents[1];F=P.parent/'firmware'/'DIY_iPod_Mono';T=P/'MonoPod.Desktop.Tests'/'native';T.mkdir(parents=True,exist_ok=True)
def put(name,s): (T/name).write_text(s.strip()+'\n',encoding='utf-8')
put('Arduino.h',r'''
#pragma once
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <chrono>
#include <cctype>
using std::min;
class String:public std::string {
public:using std::string::string;String(const std::string& s):std::string(s){}String(unsigned long long n):std::string(std::to_string(n)){}String(unsigned long n):std::string(std::to_string(n)){}String(unsigned n):std::string(std::to_string(n)){}String(int n):std::string(std::to_string(n)){}String(long n):std::string(std::to_string(n)){}
 bool isEmpty()const{return empty();}bool startsWith(const String& s)const{return rfind(s,0)==0;}bool endsWith(const String& s)const{return size()>=s.size()&&compare(size()-s.size(),s.size(),s)==0;}int indexOf(char c,int start=0)const{auto p=find(c,start);return p==npos?-1:(int)p;}int lastIndexOf(char c)const{auto p=rfind(c);return p==npos?-1:(int)p;}String substring(int from,int to=-1)const{if(from>(int)size())return "";return substr(from,to<0?npos:to-from);}void toLowerCase(){for(char& c:*this)c=(char)tolower((unsigned char)c);}void trim(){auto a=find_first_not_of(" \t\r\n"),b=find_last_not_of(" \t\r\n");*this=a==npos?"":substr(a,b-a+1);}
};
inline uint32_t millis(){return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}inline void yield(){}
''')
put('Config.h','#pragma once\n#include "Arduino.h"\n#define FEATURE_FLAC 1\n#define FEATURE_AAC 1')
put('SyncMode.h',r'''
#pragma once
enum class SyncTransport {None,UsbCdc,WiFi};
struct SyncController {SyncTransport current=SyncTransport::UsbCdc;bool canWrite(SyncTransport t)const{return current==t;}bool is(SyncTransport t)const{return current==t;}};
extern SyncController syncMode;
''')
put('SD.h',r'''
#pragma once
#include "Arduino.h"
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>
#define FILE_READ 0
#define FILE_WRITE 1
namespace fs=std::filesystem;
class File {
 struct State {fs::path path;std::fstream stream;bool directory=false,valid=false;std::vector<fs::path> entries;size_t cursor=0;};std::shared_ptr<State> s;
public:File()=default;File(fs::path p,int mode=FILE_READ):s(std::make_shared<State>()){s->path=p;s->directory=fs::is_directory(p);if(s->directory){s->valid=true;for(auto& e:fs::directory_iterator(p))s->entries.push_back(e.path());}else{s->stream.open(p,std::ios::binary|(mode==FILE_WRITE?(std::ios::out|std::ios::trunc):std::ios::in));s->valid=s->stream.is_open();}}
 explicit operator bool()const{return s&&s->valid;}bool isDirectory()const{return s&&s->directory;}size_t size()const{return *this&&!s->directory?(size_t)fs::file_size(s->path):0;}uint32_t getLastWrite()const{return 0;}const char* name()const{static std::string n;n=s?s->path.filename().string():"";return n.c_str();}
 File openNextFile(){return s&&s->cursor<s->entries.size()?File(s->entries[s->cursor++]):File();}int read(uint8_t* b,size_t n){s->stream.read((char*)b,n);return (int)s->stream.gcount();}bool seek(size_t n){s->stream.clear();s->stream.seekg(n);return s->stream.good();}String readStringUntil(char c){std::string line;std::getline(s->stream,line,c);return line;}size_t write(const uint8_t* b,size_t n){s->stream.write((const char*)b,n);return s->stream.good()?n:0;}void println(const String& t){s->stream<<t<<'\n';}void flush(){s->stream.flush();}void close(){if(s){s->stream.close();s->valid=false;}}
};
struct SDAdapter {fs::path root;std::string failRenameTarget;fs::path path(const char* s){return root/fs::path(s+1);}File open(const char* p,int mode=FILE_READ){return File(path(p),mode);}bool exists(const char* p){return fs::exists(path(p));}bool mkdir(const char* p){return fs::create_directory(path(p));}bool remove(const char* p){return fs::remove(path(p));}bool rmdir(const char* p){std::error_code e;return fs::remove(path(p),e);}bool rename(const char* a,const char* b){if(failRenameTarget==b)return false;std::error_code e;fs::rename(path(a),path(b),e);return !e;}};
extern SDAdapter SD;
''')
put('Library.h','#pragma once\nstruct MediaLibrary {bool ready=true;bool sdOK()const{return ready;}uint64_t totalBytes()const{return 64000000000ULL;}uint64_t usedBytes()const{return 1000000000ULL;}size_t size()const{return 12;}};extern MediaLibrary media;')
put('Theme.h','#pragma once\nstruct ThemeManager {static constexpr uint32_t MAX_BYTES=131072;static bool validate(const char*){return true;}};')
put('Display.h','#pragma once\nstruct Display {bool reloadTheme(){return true;}};extern Display display;')
put('Settings.h','#pragma once\nstruct Settings {struct Data {bool fontSmoothing=false;} data;};extern Settings settings;')
put('WiFi.h','#pragma once\n#include "Arduino.h"\n#define WL_CONNECTED 3\nstruct IP {String toString(){return "192.168.1.10";}};struct Wifi {int status(){return WL_CONNECTED;}IP localIP(){return {};}IP softAPIP(){return {};}};extern Wifi WiFi;')
put('sync_test.cpp',r'''
#include "SyncFiles.h"
#include "Library.h"
#include "Display.h"
#include "Settings.h"
#include "WiFi.h"
#include <iostream>
#include <stdexcept>
SDAdapter SD;MediaLibrary media;SyncController syncMode;Display display;Settings settings;Wifi WiFi;
void check(bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);}
std::string contents(const char* path){std::ifstream f(SD.path(path),std::ios::binary);return std::string(std::istreambuf_iterator<char>(f),{});}
void save(const char* path,const std::string& value){std::ofstream f(SD.path(path),std::ios::binary);f<<value;}
int main(int argc,char** argv){try{
 SD.root=argv[1];syncMode.current=SyncTransport::UsbCdc;fs::create_directories(SD.root/"Music");String out;
 check(!SyncFiles::canonical("/Music/../x",out),"Traversal accepted");check(!SyncFiles::canonical("/MonoPod/theme.mpt",out),"System write accepted");check(!SyncFiles::canonical("/mOnOpOd/boot.rgb565",out),"Case bypass");check(!SyncFiles::remove("/Music",true,SyncTransport::UsbCdc),"Protected root removed");
 syncMode.current=SyncTransport::None;check(!SyncFiles::mkdir("/Other",SyncTransport::UsbCdc),"Missing sync accepted");syncMode.current=SyncTransport::UsbCdc;
 check(SyncFiles::mkdir("/Music/Album",SyncTransport::UsbCdc),"mkdir");save("/Music/Album/keep.bin","keep");check(!SyncFiles::remove("/Music/Album",true,SyncTransport::UsbCdc),"Nonempty directory removed");
 std::string data(4097,'a');uint32_t crc=~SyncFiles::crcStep(~0u,(const uint8_t*)data.data(),data.size());
 check(SyncFiles::begin("/Music/new.mp3",data.size(),crc,false,false,SyncTransport::UsbCdc),"begin upload");check(SyncFiles::write((const uint8_t*)data.data(),1024),"write1");check(!SD.exists("/Music/new.mp3"),"Premature destination");check(SyncFiles::write((const uint8_t*)data.data()+1024,data.size()-1024),"write2");check(SyncFiles::commit(),"commit");check(contents("/Music/new.mp3")==data,"wrong content");
 check(!SyncFiles::begin("/Music/new.mp3",data.size(),crc,false,false,SyncTransport::UsbCdc),"Duplicate accepted");
 check(SyncFiles::begin("/Music/new.mp3",data.size(),crc+1,true,false,SyncTransport::UsbCdc),"replace prepare");check(SyncFiles::write((const uint8_t*)data.data(),data.size()),"replace write");check(!SyncFiles::commit(),"Bad CRC accepted");check(contents("/Music/new.mp3")==data,"Bad CRC destroyed old");
 check(SyncFiles::begin("/Music/new.mp3",data.size(),crc,true,false,SyncTransport::UsbCdc),"abort prepare");check(SyncFiles::write((const uint8_t*)data.data(),1024),"partial");SyncFiles::abort();check(contents("/Music/new.mp3")==data&&!SD.exists("/.monopod-transfer.part"),"Abort destroyed old");
 check(SyncFiles::begin("/Music/new.mp3",data.size(),crc,true,false,SyncTransport::UsbCdc),"failed rename begin");SyncFiles::write((const uint8_t*)data.data(),data.size());SD.failRenameTarget="/Music/new.mp3";check(!SyncFiles::commit(),"Injected commit rename accepted");SD.failRenameTarget="";SyncFiles::recover();check(contents("/Music/new.mp3")==data,"Rollback recovery lost old");
 fs::rename(SD.path("/Music/new.mp3"),SD.path("/.monopod-transfer.bak"));save("/.monopod-transfer.target","/Music/new.mp3\n");save("/.monopod-transfer.part","unfinished");SyncFiles::recover();check(contents("/Music/new.mp3")==data&&!SD.exists("/.monopod-transfer.bak"),"Power loss recovery");
 check(SyncFiles::rename("/Music/new.mp3","/Music/renamed.mp3",SyncTransport::UsbCdc),"rename");uint8_t block[1024];size_t got=0;check(SyncFiles::read("/Music/renamed.mp3",4096,block,1024,got,SyncTransport::UsbCdc)&&got==1&&block[0]=='a',"range read tail");check(!SyncFiles::read("/Music/renamed.mp3",99999,block,1024,got,SyncTransport::UsbCdc),"Invalid range accepted");check(SyncFiles::remove("/Music/renamed.mp3",false,SyncTransport::UsbCdc),"delete");
 std::cout<<"PASS actual SyncFiles.cpp: paths, permissions, streaming, CRC, replacement, abort, rename failure, power recovery, ranges, delete\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
''')

put('UI.h','#pragma once\nstruct UI {void notify(const String&){}void begin(){}void redraw(){}};extern UI ui;')
put('IpodIdentity.h','#pragma once\nnamespace IpodIdentity {inline const char* guid(){return "test-guid";}}')
put('WiFiSync.h','#pragma once\nstruct WiFiSyncService {bool active()const{return false;}};extern WiFiSyncService wifiSync;')
with (T/'Config.h').open('a') as f:f.write('\n#define FEATURE_USB_SYNC 1\n')
with (T/'Arduino.h').open('a') as f:f.write(r"""
#include <deque>
struct TestSerial {
 std::deque<uint8_t> input;std::string output;
 int available(){return (int)input.size();}int read(){if(input.empty())return -1;int v=input.front();input.pop_front();return v;}
 void print(const String& s){output+=s;}void println(const String& s){output+=s;output+='\n';}size_t write(const uint8_t* b,size_t n){output.append((const char*)b,n);return n;}explicit operator bool()const{return true;}
};extern TestSerial Serial;
""")
with (T/'Library.h').open('w') as f:f.write(r"""
#pragma once
struct MediaLibrary {bool ready=true;bool sdOK()const{return ready;}uint64_t totalBytes()const{return 64000000000ULL;}uint64_t usedBytes()const{return 1000000000ULL;}size_t size()const{return 12;}void rescan(){}static bool isAudio(String p){p.toLowerCase();return p.endsWith(".mp3")||p.endsWith(".wav")||p.endsWith(".flac")||p.endsWith(".aac");}};extern MediaLibrary media;
""")
with (T/'Settings.h').open('w') as f:f.write('#pragma once\nstruct Settings {struct Data {bool fontSmoothing=false;} data;void markDirty(){}};extern Settings settings;')
with (T/'SyncMode.h').open('w') as f:f.write(r"""
#pragma once
enum class SyncTransport {None,UsbCdc,WiFi};struct SyncController {SyncTransport current=SyncTransport::None;bool canWrite(SyncTransport t)const{return current==t;}bool is(SyncTransport t)const{return current==t;}bool enter(SyncTransport t){if(current!=SyncTransport::None&&current!=t)return false;current=t;return true;}bool leave(SyncTransport){current=SyncTransport::None;return true;}};extern SyncController syncMode;
""")
put('usb_test.cpp',r"""
#include "UsbSync.h"
#include "SyncFiles.h"
#include "Library.h"
#include "Display.h"
#include "Settings.h"
#include "WiFi.h"
#include "UI.h"
#include "WiFiSync.h"
#include <iostream>
#include <stdexcept>
SDAdapter SD;MediaLibrary media;SyncController syncMode;Display display;Settings settings;Wifi WiFi;UI ui;WiFiSyncService wifiSync;TestSerial Serial;
void check(bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);}
std::string send(const std::string& text){Serial.output.clear();for(uint8_t c:text)Serial.input.push_back(c);while(Serial.available())usbSync.loop();return Serial.output;}
std::string command(const std::string& text){return send("MPOD1 "+text+"\n");}
std::string hex(const std::string& s){const char* h="0123456789ABCDEF";std::string out;for(uint8_t c:s){out+=h[c>>4];out+=h[c&15];}return out;}
std::string contents(const char* path){std::ifstream f(SD.path(path),std::ios::binary);return std::string(std::istreambuf_iterator<char>(f),{});}
int main(int argc,char** argv){try{
 SD.root=argv[1];fs::create_directories(SD.root/"Music");usbSync.begin();
 check(command("HELLO").find("READY 4")!=std::string::npos,"HELLO");check(syncMode.current==SyncTransport::None,"HELLO entered Sync");
 check(command("X INFO").find("64000000000")!=std::string::npos,"64-bit info");check(command("X MKDIR "+hex("/Music/Album")).find("ERR SYNC_REQUIRED")!=std::string::npos,"Missing sync gate");
 check(command("SYNC START").find("OK SYNC_STARTED")!=std::string::npos,"Sync start");
 check(command("X MKDIR "+hex("/Music/Album")).find("OK DONE")!=std::string::npos,"MKDIR parser");
 std::string data(3079,'x');data.replace(10,15,"MPOD1 SYNC END\n");uint32_t crc=~SyncFiles::crcStep(~0u,(const uint8_t*)data.data(),data.size());char h[9];snprintf(h,sizeof(h),"%08X",crc);std::string path="/Music/Album/test.mp3";
 check(command("X PUT "+hex(path)+" "+std::to_string(data.size())+" "+h+" 0")=="MPOD1 NEXT 1024\n","PUT parser");
 for(size_t at=0;at<data.size();at+=1024){std::string reply=send(data.substr(at,1024));check(reply.find(at+1024>=data.size()?"OK SAVED":"NEXT")!=std::string::npos,"Binary handshake");}
 check(contents(path.c_str())==data,"Payload mismatch");check(syncMode.current==SyncTransport::UsbCdc,"Binary parsed as command");
 std::string stat=command("X STAT "+hex(path));check(stat.find("3079")!=std::string::npos,"STAT parser");
 check(command("X LIST "+hex("/Music/Album")+" 0 32").find("test.mp3")!=std::string::npos,"LIST parser");
 std::string read=command("X READ "+hex(path)+" 3000 100");check(read.find("DATA 79 ")!=std::string::npos&&read.substr(read.size()-79)==data.substr(3000),"READ framing");
 check(command("X SMOOTH 1").find("OK SMOOTHING")!=std::string::npos&&settings.data.fontSmoothing,"Smoothing parser");
 check(command("X RENAME "+hex(path)+" "+hex("/Music/renamed.mp3")).find("OK DONE")!=std::string::npos,"RENAME parser");
 check(command("X DELETE "+hex("/Music/renamed.mp3")).find("OK DONE")!=std::string::npos,"DELETE parser");
 check(command("X RMDIR "+hex("/Music/Album")).find("OK DONE")!=std::string::npos,"RMDIR parser");
 check(command("SYNC END").find("OK SYNC_ENDED")!=std::string::npos,"Sync end");
 std::cout<<"PASS actual UsbSync.cpp + SyncFiles.cpp: explicit Sync, INFO, binary handshake, LIST/STAT/READ, MKDIR/RENAME/DELETE/RMDIR, smoothing, END\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
""")
# Compile exact firmware source from a temporary include environment.
build=P/'work'/'native';build.mkdir(parents=True,exist_ok=True)
for file in ['SyncFiles.cpp','SyncFiles.h','UsbSync.cpp','UsbSync.h']: shutil.copy2(F/file,T/file)
bat=build/'compile.cmd'
bat.write_text(f'''@echo off
call "D:\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul
cl /nologo /EHsc /std:c++17 /utf-8 /I"{T}" "{T/'SyncFiles.cpp'}" "{T/'sync_test.cpp'}" /Fe:"{build/'sync_test.exe'}" /Fo:{build}/
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /utf-8 /I"{T}" "{T/'SyncFiles.cpp'}" "{T/'UsbSync.cpp'}" "{T/'usb_test.cpp'}" /Fe:"{build/'usb_test.exe'}" /Fo:{build}/
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /utf-8 /I"{F.parent/'tests'/'stubs'}" /I"{F}" "{F/'Theme.cpp'}" "{F.parent/'tests'/'theme_loader_test.cpp'}" /Fe:"{build/'theme_test.exe'}" /Fo:{build}/
''')

# Existing firmware algorithm tests are compiled with the Windows toolchain too.
with bat.open('a') as commandFile:
 for name in ['dev_console_protocol_test','backlight_math_test','wifi_signal_native_test','monofont_native_test']:
  commandFile.write(f'\nif errorlevel 1 exit /b 1\ncl /nologo /EHsc /std:c++17 /utf-8 /I"{F.parent/"tests"/"stubs"}" /I"{F}" "{F.parent/"tests"/(name+".cpp")}" /Fe:"{build/(name+".exe")}" /Fo:{build}/\n')
run=subprocess.run(['cmd','/c',str(bat)],capture_output=True,text=True,errors='replace');(P/'docs'/'native-build.log').write_text(run.stdout+run.stderr,encoding='utf-8');print(run.stdout[-2500:]);
if run.returncode:sys.exit(run.returncode)
results=[]
import tempfile
scratch=Path(tempfile.mkdtemp(prefix='sd-',dir=build))
for cmd in [[str(build/'sync_test.exe'),str(scratch)],[str(build/'usb_test.exe'),str(scratch/'usb')]]+[[str(build/'theme_test.exe'),str(P/'docs'/'theme-fixtures'/name)]for name in ['csharp-mpt1.mpt','csharp-mpt2.mpt']]:
 r=subprocess.run(cmd,capture_output=True,text=True,encoding='utf-8',errors='replace');print(r.stdout,r.stderr);results.append(dict(command=cmd,exit_code=r.returncode,output=r.stdout+r.stderr))


for name in ['dev_console_protocol_test','backlight_math_test','wifi_signal_native_test','monofont_native_test']:
 r=subprocess.run([str(build/(name+'.exe'))],capture_output=True,text=True,errors='replace')
 results.append(dict(command=[name],exit_code=r.returncode,output=r.stdout+r.stderr))
# Independently mutate the fixture; invalid glyph tests repair CRC so they exercise bounds validation.
import struct,zlib
fixture=(P/'docs'/'theme-fixtures'/'csharp-mpt2.mpt').read_bytes()
for name,kind in [('truncated',0),('crc',1),('glyph',2),('version',3)]:
 bad=bytearray(fixture)
 if kind==0:bad=bad[:43]
 elif kind==1:bad[-1]^=255
 elif kind==2:bad[48]=255;struct.pack_into('<I',bad,40,zlib.crc32(bad[44:]))
 else:bad[4]=99
 file=build/(name+'.mpt');file.write_bytes(bad)
 r=subprocess.run([str(build/'theme_test.exe'),str(file)],capture_output=True,text=True,errors='replace')
 results.append(dict(command=['actual Theme.cpp reject '+name],exit_code=0 if r.returncode==1 else 1,observed_exit_code=r.returncode,expected_exit_code=1))
(P/'docs'/'native-tests.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
sys.exit(any(r['exit_code'] for r in results))


