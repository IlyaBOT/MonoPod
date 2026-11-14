#include "SyncFiles.h"
#include "Library.h"
#include "Theme.h"
#include "Display.h"
#include "Settings.h"
#include "ClockService.h"
#include <WiFi.h>
namespace SyncFiles {
String error;
namespace {
const char* PART="/.monopod-transfer.part";
const char* BACKUP="/.monopod-transfer.bak";
const char* JOURNAL="/.monopod-transfer.target";
File output;String target;uint32_t expected=0,checksum=0,received=0,runningCRC=~0u,lastWrite=0;
bool active=false,isTheme=false;
bool fail(const char* text){error=text;return false;}
bool protectedRoot(String p){p.toLowerCase();return p=="/"||p=="/music"||p=="/playlists"||p=="/themes"||p=="/monopod";}
String number(uint64_t n){char buf[24];snprintf(buf,sizeof(buf),"%llu",(unsigned long long)n);return String(buf);}
String entry(File& f,const String& path){return "{\"name\":"+quote(path.substring(path.lastIndexOf('/')+1))+",\"path\":"+quote(path)+",\"directory\":"+(f.isDirectory()?"true":"false")+",\"size\":"+number(f.size())+",\"modified\":"+number(f.getLastWrite())+"}";}
}
uint32_t crcStep(uint32_t c,const uint8_t* b,size_t n){for(size_t i=0;i<n;i++){c^=b[i];for(uint8_t j=0;j<8;j++)c=(c>>1)^((c&1)?0xedb88320UL:0);}return c;}
String quote(const String& s){String out="\"";for(size_t i=0;i<s.length();i++){uint8_t c=s[i];if(c=='"'||c=='\\'){out+='\\';out+=(char)c;}else if(c<32){char h[7];snprintf(h,sizeof(h),"\\u%04x",c);out+=h;}else out+=(char)c;}return out+'"';}
bool canonical(const String& in,String& out,bool theme){
 out="";if(in.isEmpty()||in[0]!='/'||in.length()>240)return fail("INVALID_PATH");
 int start=1;out="/";
 while(start<(int)in.length()){
  int end=in.indexOf('/',start);if(end<0)end=in.length();String part=in.substring(start,end);start=end+1;
  if(part.isEmpty())continue;
  if(part=="."||part==".."||part[0]=='.'||part.endsWith(".")||part.endsWith(" ")||part.length()>90)return fail("INVALID_PATH");
  for(size_t i=0;i<part.length();i++){uint8_t c=part[i];if(c<32||c==127||strchr("\\:<>\"|*?",c))return fail("INVALID_PATH");}
  if(out.length()>1)out+='/';out+=part;
 }
 String low=out;low.toLowerCase();
 // Explicit user media asset; permit generic USB/Wi-Fi file upload and download.
 // Keep theme.mpt and other /MonoPod private files behind their own APIs.
 if(low=="/monopod" || low=="/monopod/badapple.bav")return true;
 if(theme && low=="/monopod/theme.mpt")return true;
 if(low=="/monopod"||low.startsWith("/monopod/")||low=="/ipod_control"||low.startsWith("/ipod_control/")||low.startsWith("/system volume information")||low.startsWith("/$recycle.bin")||low.startsWith("/recycler"))return fail("PROTECTED_PATH");
 return true;
}
String fromHex(const String& s){if(s.length()%2||s.length()>480)return "";String out;for(size_t i=0;i<s.length();i+=2){char a=s[i],b=s[i+1];auto digit=[](char c)->int {if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};int x=digit(a),y=digit(b);if(x<0||y<0||!(x*16+y))return "";out+=(char)(x*16+y);}return out;}
bool gate(SyncTransport who){error="";if(!media.sdOK())return fail("NO_SD");if(!syncMode.canWrite(who))return fail("SYNC_REQUIRED");return true;}
String info(){
 String mode=syncMode.is(SyncTransport::UsbCdc)?"usb":syncMode.is(SyncTransport::WiFi)?"wifi":"none";
 uint64_t total=media.sdOK()?media.totalBytes():0,used=media.sdOK()?media.usedBytes():0;
 String formats="[\".mp3\",\".wav\"";
#if FEATURE_FLAC
 formats+=",\".flac\"";
#endif
#if FEATURE_AAC
 formats+=",\".aac\"";
#endif
 formats+="]";
 return "{\"firmware\":\"0.10.6.8\",\"protocol\":4,\"sd_ready\":"+String(media.sdOK()?"true":"false")+",\"total_bytes\":"+number(total)+",\"free_bytes\":"+number(total>=used?total-used:0)+",\"tracks\":"+String((unsigned)media.size())+",\"sync_mode\":"+quote(mode)+",\"ip\":"+quote(WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():WiFi.softAPIP().toString())+",\"font_smoothing\":"+String(settings.data.fontSmoothing?"true":"false")+",\"ntp_enabled\":"+String(clockService.ntpEnabled()?"true":"false")+",\"ntp_server\":"+quote(clockService.ntpServer())+",\"timezone\":"+quote(clockService.timezone())+",\"clock_unix\":"+number(clockService.valid()?(uint64_t)time(nullptr):0)+",\"audio_formats\":"+formats+",\"capabilities\":[\"clock_sync\",\"ntp\",\"ntp_server\",\"list\",\"stat\",\"read\",\"fs_upload\",\"upload\",\"delete\",\"mkdir\",\"rename\",\"theme\",\"theme_get\",\"scan\",\"metadata_ranges\",\"smoothing\",\"sync_end\",\"ota_update_v1\"]}";
}
String stat(const String& path,SyncTransport who){String p;if(!gate(who)||!canonical(path,p,path=="/MonoPod/theme.mpt"))return "";File f=SD.open(p.c_str());if(!f){fail("NOT_FOUND");return "";}String result=entry(f,p);f.close();return result;}
String list(const String& path,uint32_t offset,uint16_t limit,SyncTransport who){
 String p;if(!gate(who)||!canonical(path,p))return "";if(!limit||limit>32){fail("INVALID_LIMIT");return "";}File d=SD.open(p.c_str());if(!d||!d.isDirectory()){if(d)d.close();fail("NOT_DIRECTORY");return "";}
 String result="{\"entries\":[";uint32_t seen=0;uint16_t count=0;bool more=false;
 File f=d.openNextFile();while(f){String name=f.name();name=name.substring(name.lastIndexOf('/')+1);String full=(p=="/"?"/":p+"/")+name,checked;
  if(canonical(full,checked)){if(seen++>=offset){if(count==limit){more=true;f.close();break;}if(count++)result+=',';result+=entry(f,full);}}
  f.close();f=d.openNextFile();yield();
 }d.close();error="";result+="],\"next\":";result+=more?String(offset+count):String("-1");return result+"}";
}
bool read(const String& path,uint32_t offset,uint8_t* buffer,size_t count,size_t& got,SyncTransport who){String p;got=0;if(!gate(who)||!canonical(path,p,path=="/MonoPod/theme.mpt"))return false;if(count>65536)return fail("INVALID_COUNT");File f=SD.open(p.c_str());if(!f)return fail("NOT_FOUND");if(f.isDirectory()||offset>f.size()){f.close();return fail("INVALID_RANGE");}if(offset==f.size()){f.close();return true;}if(!f.seek(offset)){f.close();return fail("SEEK_FAILED");}size_t n=min(count,(size_t)(f.size()-offset));int actual=f.read(buffer,n);f.close();if(actual<0||(size_t)actual!=n)return fail("SD_READ");got=n;return true;}
bool mkdir(const String& path,SyncTransport who){String p;if(!gate(who)||active||!canonical(path,p))return false;if(SD.exists(p.c_str()))return fail("ALREADY_EXISTS");return SD.mkdir(p.c_str())||fail("MKDIR_FAILED");}
bool remove(const String& path,bool directory,SyncTransport who){String p;if(!gate(who)||active||!canonical(path,p))return false;if(protectedRoot(p))return fail("PROTECTED_ROOT");File f=SD.open(p.c_str());if(!f)return fail("NOT_FOUND");if(f.isDirectory()!=directory){f.close();return fail("WRONG_TYPE");}if(directory){File child=f.openNextFile();if(child){child.close();f.close();return fail("DIRECTORY_NOT_EMPTY");}}f.close();return (directory?SD.rmdir(p.c_str()):SD.remove(p.c_str()))||fail("DELETE_FAILED");}
bool rename(const String& path,const String& dest,SyncTransport who){String p,q;if(!gate(who)||active||!canonical(path,p)||!canonical(dest,q))return false;if(protectedRoot(p)||protectedRoot(q))return fail("PROTECTED_ROOT");if(SD.exists(q.c_str()))return fail("ALREADY_EXISTS");if(!SD.exists(p.c_str()))return fail("NOT_FOUND");if(q.startsWith(p+"/"))return fail("INVALID_PATH");return SD.rename(p.c_str(),q.c_str())||fail("RENAME_FAILED");}
void recover(){
 if(!media.sdOK()||active)return;

 if(SD.exists(JOURNAL)){File j=SD.open(JOURNAL);String p=j?j.readStringUntil('\n'):String();if(j)j.close();p.trim();String checked;if(!canonical(p,checked,p=="/MonoPod/theme.mpt"))return;
  if(SD.exists(BACKUP)){if(SD.exists(p.c_str())){if(!SD.remove(BACKUP))return;}else if(!SD.rename(BACKUP,p.c_str()))return;}
  if(!SD.exists(BACKUP))SD.remove(JOURNAL);
 }
 SD.remove(PART);
}
bool begin(const String& path,uint32_t size,uint32_t crc,bool replace,bool theme,SyncTransport who){
 if(!gate(who))return false;if(active)return fail("TRANSFER_ACTIVE");String p;if(!canonical(path,p,theme))return false;
 if(theme && p!="/MonoPod/theme.mpt")return fail("INVALID_THEME_PATH");if(p=="/"||size>1073741824UL|| (theme&&size>ThemeManager::MAX_BYTES+44))return fail("INVALID_SIZE");
 recover();if(SD.exists(BACKUP)||SD.exists(JOURNAL))return fail("RECOVERY_PENDING");
 if(SD.exists(p.c_str())){File f=SD.open(p.c_str());bool dir=f&&f.isDirectory();if(f)f.close();if(dir)return fail("IS_DIRECTORY");if(!replace)return fail("ALREADY_EXISTS");}
 uint64_t total=media.totalBytes(),used=media.usedBytes();if(total<used||total-used<(uint64_t)size+4096)return fail("NO_SPACE");
 int slash=p.lastIndexOf('/');String parent=slash>0?p.substring(0,slash):"/";
 if(theme && !SD.exists("/MonoPod"))SD.mkdir("/MonoPod");
 if(!SD.exists(parent.c_str()))return fail("PARENT_NOT_FOUND");
 SD.remove(PART);output=SD.open(PART,FILE_WRITE);if(!output)return fail("OPEN_FAILED");
 target=p;expected=size;checksum=crc;received=0;runningCRC=~0u;active=true;isTheme=theme;lastWrite=millis();return true;
}
bool write(const uint8_t* data,size_t n){if(!active)return fail("NO_TRANSFER");if(n>1073741824UL-received||(n>expected-received)|| (isTheme && received+n>ThemeManager::MAX_BYTES+44))return fail("INVALID_SIZE");
 if(output.write(data,n)!=n)return fail("SD_WRITE");runningCRC=crcStep(runningCRC,data,n);received+=n;lastWrite=millis();return true;}
void abort(){if(output)output.close();if(active)SD.remove(PART);active=false;target="";}
bool busy(){return active;}
void tick(){if(active && (uint32_t)(millis()-lastWrite)>15000){abort();error="TIMEOUT";}}
bool commit(){
 if(!active)return fail("NO_TRANSFER");output.flush();output.close();
 File verified=SD.open(PART,FILE_READ);bool validSize=verified && verified.size()==received;if(verified)verified.close();if(!validSize){abort();return fail("SD_SIZE_MISMATCH");}
 if(received!=expected || ~runningCRC!=checksum){abort();return fail("SIZE_OR_CRC_MISMATCH");}
 if(isTheme&&!ThemeManager::validate(PART)){abort();return fail("INVALID_THEME_OR_CRC");}
 String dest=target;bool old=SD.exists(dest.c_str()),installTheme=isTheme;
 File j=SD.open(JOURNAL,FILE_WRITE);if(!j){abort();return fail("JOURNAL_WRITE");}j.println(dest);j.flush();j.close();
 if(old&&!SD.rename(dest.c_str(),BACKUP)){SD.remove(JOURNAL);abort();return fail("BACKUP_RENAME");}
 if(!SD.rename(PART,dest.c_str())){if(old)SD.rename(BACKUP,dest.c_str());if(!SD.exists(BACKUP))SD.remove(JOURNAL);abort();return fail("COMMIT_FAILED");}
 active=false;target="";
 if(installTheme&&!display.reloadTheme()){
  SD.remove(dest.c_str());if(old)SD.rename(BACKUP,dest.c_str());display.reloadTheme();if(!SD.exists(BACKUP))SD.remove(JOURNAL);return fail("THEME_ACTIVATION_FAILED");
 }
 if(old&&!SD.remove(BACKUP))return fail("BACKUP_CLEANUP_PENDING");
 if(!SD.remove(JOURNAL))return fail("JOURNAL_CLEANUP_PENDING");return true;
}
}

