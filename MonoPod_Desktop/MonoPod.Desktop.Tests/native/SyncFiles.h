#pragma once
#include "Config.h"
#include "SyncMode.h"
#include <SD.h>
namespace SyncFiles {
extern String error;
bool canonical(const String& input,String& output,bool theme=false);
String fromHex(const String& hex);
String quote(const String& s);
String info();
String stat(const String& path,SyncTransport who);
String list(const String& path,uint32_t offset,uint16_t limit,SyncTransport who);
bool gate(SyncTransport who);
bool read(const String& path,uint32_t offset,uint8_t* buffer,size_t count,size_t& got,SyncTransport who);
bool mkdir(const String& path,SyncTransport who);
bool remove(const String& path,bool directory,SyncTransport who);
bool rename(const String& path,const String& dest,SyncTransport who);
void recover();
bool begin(const String& path,uint32_t size,uint32_t crc,bool replace,bool theme,SyncTransport who,bool legacy=false);
bool write(const uint8_t* data,size_t count);
bool commit();
void abort();
bool busy();
void tick();
uint32_t crcStep(uint32_t crc,const uint8_t* data,size_t count);
}
