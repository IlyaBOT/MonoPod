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
