#pragma once
// MonoPodDB v1: CRC-checked portable TSV cache. Internal SD cache, not a SQL database.
// Never mutate /MonoPod/MonoPodDB from Desktop: use /api/library/{info,db,refresh}.
#include "Types.h"
#include <SD.h>
#include <vector>
namespace MonoPodDB {
constexpr const char* PATH="/MonoPod/MonoPodDB";
constexpr const char* TEMP="/MonoPod/.MonoPodDB.tmp";
constexpr const char* BACKUP="/MonoPod/.MonoPodDB.bak";
constexpr uint16_t VERSION=1;
inline uint32_t crcStep(uint32_t crc,const uint8_t* data,size_t len){
 for(size_t i=0;i<len;i++){crc^=data[i];for(uint8_t j=0;j<8;j++)crc=(crc>>1)^((crc&1)?0xEDB88320UL:0);}
 return crc;
}
inline String number(uint64_t n){char b[24];snprintf(b,sizeof(b),"%llu",(unsigned long long)n);return String(b);}
inline String hex32(uint32_t n){char b[9];snprintf(b,sizeof(b),"%08lX",(unsigned long)n);return String(b);}
inline bool unumber(const String& s,uint64_t& n,uint64_t max){
 if(!s.length()||s.length()>20)return false;
 uint64_t v=0;for(size_t i=0;i<s.length();i++){char c=s[i];if(c<'0'||c>'9')return false;uint8_t d=c-'0';if(v>(max-d)/10)return false;v=v*10+d;}
 n=v;return true;
}
inline bool hexNumber(const String& s,uint32_t& out){
 if(s.length()!=8)return false;uint32_t v=0;for(size_t i=0;i<s.length();i++){char c=s[i];int d=c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:c>='a'&&c<='f'?c-'a'+10:-1;if(d<0)return false;v=(v<<4)|d;}out=v;return true;
}
inline String esc(const String& s){
 String out;out.reserve(s.length()+8);for(size_t i=0;i<s.length();i++){
  uint8_t c=s[i];if(c=='%'||c=='\t'||c=='\r'||c=='\n'||c<32){char b[4];snprintf(b,sizeof(b),"%%%02X",c);out+=b;}else out+=(char)c;
 }return out;
}
inline bool unesc(const String& s,String& out){
 out="";out.reserve(s.length());for(size_t i=0;i<s.length();i++){
  if(s[i]!='%'){out+=s[i];continue;}
  if(i+2>=s.length())return false;
  auto d=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;};
  int a=d(s[i+1]),b=d(s[i+2]);if(a<0||b<0||a*16+b==0)return false;
  out+=(char)(a*16+b);i+=2;
 }return true;
}
inline bool line(File& f,String& out,size_t max=2048){
 out="";while(f.available()){
  int c=f.read();if(c=='\n')return true;
  if(c=='\r')continue;
  if(out.length()>=max)return false;
  out+=(char)c;
 }return false;
}
inline bool fields(const String& row,std::vector<String>& out,size_t maxFields=24){
 out.clear();int from=0;while(true){if(out.size()>=maxFields)return false;int n=row.indexOf('\t',from);out.push_back(n<0?row.substring(from):row.substring(from,n));if(n<0)break;from=n+1;}return true;
}
inline String row(const Track& t){
 String s="T\t";
 s+=esc(t.path)+"\t"+number(t.fileSize)+"\t"+number(t.modifiedUnix)+"\t"+hex32(t.fingerprint)+"\t"+hex32(t.fullCrc32);
 s+='\t';s+=number(t.durationMs);s+='\t';s+=number(t.year);s+='\t';s+=number(t.trackNumber);
 s+='\t';s+=number(t.sampleRate);s+='\t';s+=number(t.bitrateKbps);s+='\t';s+=esc(t.codec);
 s+='\t';s+=esc(t.extension);s+='\t';s+=esc(t.title);s+='\t';s+=esc(t.artist);
 s+='\t';s+=esc(t.album);s+='\t';s+=esc(t.albumArtist);s+='\t';s+=esc(t.genre);
 s+='\t';s+=number(t.flacTotalSamples);s+='\t';s+=number(t.flacSampleRate);return s;
}
inline bool parseRow(const String& row,Track& t){
 std::vector<String> a;if(!fields(row,a)||a.size()!=20||a[0]!="T")return false;
 if(!unesc(a[1],t.path)||t.path.length()<2||t.path[0]!='/'||!unesc(a[11],t.codec)||!unesc(a[12],t.extension)||
    !unesc(a[13],t.title)||!unesc(a[14],t.artist)||!unesc(a[15],t.album)||!unesc(a[16],t.albumArtist)||!unesc(a[17],t.genre))return false;
 uint64_t v=0;
 auto numeric=[&](int index,uint64_t max,uint64_t& dest)->bool {return unumber(a[index],dest,max);};
 if(!numeric(2,0xffffffffULL,v))return false;t.fileSize=v;
 if(!numeric(3,0xffffffffULL,v))return false;t.modifiedUnix=v;
 if(!hexNumber(a[4],t.fingerprint)||!hexNumber(a[5],t.fullCrc32))return false;
 if(!numeric(6,0xffffffffULL,v))return false;t.durationMs=v;
 if(!numeric(7,65535,v))return false;t.year=v;
 if(!numeric(8,65535,v))return false;t.trackNumber=v;
 if(!numeric(9,0xffffffffULL,v))return false;t.sampleRate=v;
 if(!numeric(10,0xffffffffULL,v))return false;t.bitrateKbps=v;
 if(!numeric(18,0xffffffffffffffffULL,v))return false;t.flacTotalSamples=v;
 if(!numeric(19,0xffffffffULL,v))return false;t.flacSampleRate=v;
 t.durationSeconds=(t.durationMs+500)/1000;
 int slash=t.path.lastIndexOf('/');t.folder=slash>0?t.path.substring(0,slash):"/";
 return true;
}
inline void recover(){
 if(!SD.exists("/MonoPod"))return;
 if(!SD.exists(PATH)&&SD.exists(BACKUP))SD.rename(BACKUP,PATH);
 if(SD.exists(PATH)&&SD.exists(BACKUP))SD.remove(BACKUP);
 if(SD.exists(TEMP))SD.remove(TEMP);
}
inline bool load(std::vector<Track>& out,uint32_t& generation){
 out.clear();generation=0;recover();File f=SD.open(PATH,FILE_READ);if(!f)return false;
 String s;std::vector<String>a;uint64_t v=0,count=0,gen=0;bool ok=false;
 if(!line(f,s)||!fields(s,a)||a.size()!=5||a[0]!="MPODDB"||a[1]!="1"||!unumber(a[2],gen,0xffffffffULL)||!unumber(a[3],count,Config::MAX_TRACKS)){f.close();return false;}
 uint32_t crc=~0u;out.reserve((size_t)count);
 for(size_t i=0;i<count;i++){
  Track t;if(!line(f,s)||!parseRow(s,t)){out.clear();f.close();return false;}
  crc=crcStep(crc,(const uint8_t*)s.c_str(),s.length());const uint8_t lf='\n';crc=crcStep(crc,&lf,1);
  out.push_back(t);
 }
 if(line(f,s)&&fields(s,a)&&a.size()==3&&a[0]=="END"&&unumber(a[1],v,Config::MAX_TRACKS)&&v==count){uint32_t stored=0;ok=hexNumber(a[2],stored)&&stored==~crc&&f.available()==0;}
 f.close();if(!ok){out.clear();return false;}generation=gen;return true;
}
inline bool save(const std::vector<Track>& tracks,uint32_t generation){
 if(!SD.exists("/MonoPod")&&!SD.mkdir("/MonoPod"))return false;
 recover();File f=SD.open(TEMP,FILE_WRITE);if(!f)return false;
 String head="MPODDB\t1\t"+number(generation)+"\t"+number(tracks.size())+"\t0\n";
 bool ok=f.write((const uint8_t*)head.c_str(),head.length())==head.length();
 uint32_t crc=~0u;
 for(const auto& t:tracks){if(!ok)break;String r=row(t)+"\n";
  if(r.length()>2048||f.write((const uint8_t*)r.c_str(),r.length())!=r.length()){ok=false;break;}
  crc=crcStep(crc,(const uint8_t*)r.c_str(),r.length());
 }
 String tail="END\t"+number(tracks.size())+"\t"+hex32(~crc)+"\n";
 if(ok)ok=f.write((const uint8_t*)tail.c_str(),tail.length())==tail.length();
 f.flush();f.close();if(!ok){SD.remove(TEMP);return false;}
 if(SD.exists(BACKUP))SD.remove(BACKUP);
 bool had=SD.exists(PATH);if(had&&!SD.rename(PATH,BACKUP)){SD.remove(TEMP);return false;}
 if(!SD.rename(TEMP,PATH)){if(had)SD.rename(BACKUP,PATH);return false;}
 if(had)SD.remove(BACKUP);return true;
}
inline uint32_t dbSize(){File f=SD.open(PATH,FILE_READ);if(!f)return 0;uint32_t n=f.size();f.close();return n;}
inline bool read(uint32_t pos,uint8_t* dst,size_t count,size_t& got){
 got=0;File f=SD.open(PATH,FILE_READ);if(!f)return false;
 if(pos>f.size()||count>16384||!f.seek(pos)){f.close();return false;}
 got=f.read(dst,min(count,(size_t)(f.size()-pos)));f.close();return true;
}
inline uint32_t fingerprint(const String& path){
 File f=SD.open(path,FILE_READ);if(!f)return 0;const uint32_t size=f.size();
 uint8_t b[1024];uint32_t crc=~0u;
 // Sample the first and last 16 KiB, to catch common ID3/Vorbis tag edits.
 uint32_t span=min(16384UL,size),read=0;
 while(read<span){size_t n=min((uint32_t)sizeof(b),span-read);size_t got=f.read(b,n);if(!got)break;crc=crcStep(crc,b,got);read+=got;}
 if(size>span){uint32_t tail=min(16384UL,size-span);if(!f.seek(size-tail)){f.close();return 0;}read=0;
  while(read<tail){size_t n=min((uint32_t)sizeof(b),tail-read);size_t got=f.read(b,n);if(!got)break;crc=crcStep(crc,b,got);read+=got;}}
 f.close();crc=crcStep(crc,(const uint8_t*)&size,sizeof(size));return ~crc;
}
inline bool validTime(uint32_t value){return value>=378691200UL&&value<=4102444800UL;}
}
