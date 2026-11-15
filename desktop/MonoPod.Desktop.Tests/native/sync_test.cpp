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
