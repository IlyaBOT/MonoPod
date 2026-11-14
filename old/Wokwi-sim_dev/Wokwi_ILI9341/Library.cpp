#include "Library.h"
#include "Config.h"
#include <SPI.h>
#include <algorithm>
#include <cstring>
MediaLibrary media;
static String clipTag(String s) {s.trim(); if(s.length()>80) s.remove(80);return s;}
static String noExt(String n) {int slash=n.lastIndexOf('/');if(slash>=0)n=n.substring(slash+1);int dot=n.lastIndexOf('.');if(dot>0)n.remove(dot);return n;}
static bool strLess(String a,String b) {a.toLowerCase(); b.toLowerCase();return a.compareTo(b)<0;}
static uint32_t bigEndian4(const uint8_t *b) {return ((uint32_t)b[0]<<24)|((uint32_t)b[1]<<16)|((uint32_t)b[2]<<8)|b[3];}
static String id3Text(File &f,uint32_t len) {
  if(!len || len>1024) return String();
  const uint8_t enc=f.read();len--;
  // ID3 text encoding 0 = Latin1, 3 = UTF-8; UTF-16 intentionally falls back to filenames.
  if(enc!=0 && enc!=3) return String();
  String out;out.reserve(min((uint32_t)80,len));
  for(uint32_t i=0;i<len && out.length()<80;i++) {
    int c=f.read();if(c<=0)break;
    if(enc==0 && c>=0x80) { // Latin1 -> UTF-8
      out += (char)(0xC0 | (c >> 6));out += (char)(0x80 | (c & 0x3F));
    } else out+=(char)c;
  }
  return clipTag(out);
}
bool MediaLibrary::isAudio(const String &name) {
  String n=name; n.toLowerCase();
  return n.endsWith(".mp3") || n.endsWith(".wav") || n.endsWith(".flac") || n.endsWith(".aac");
}
String MediaLibrary::parent(const String &p) {int at=p.lastIndexOf('/');return at>0 ? p.substring(0,at) : String("/");}
bool MediaLibrary::begin() {
  pinMode(Config::PIN_TFT_CS,OUTPUT);digitalWrite(Config::PIN_TFT_CS,HIGH);
  pinMode(Config::PIN_SD_CS,OUTPUT);digitalWrite(Config::PIN_SD_CS,HIGH);
  mounted=SD.begin(Config::PIN_SD_CS,SPI,Config::SPI_SD_HZ);
  if(!mounted) {error="microSD mount failed";return false;}
  rescan();return true;
}
void MediaLibrary::rescan() {
  tracks.clear();if(!mounted)return;
  error="";scanFolder("/Music",0);
  if(tracks.empty()) scanFolder("/",0);
  std::sort(tracks.begin(),tracks.end(),[](const Track &a,const Track &b){return strLess(a.title,b.title);});
  if(tracks.size()>=Config::MAX_TRACKS) error="Library limit reached";
}
void MediaLibrary::scanFolder(const String &path,uint8_t depth) {
  if(depth>Config::MAX_SCAN_DEPTH || tracks.size()>=Config::MAX_TRACKS) return;
  File dir=SD.open(path.c_str());if(!dir || !dir.isDirectory()) {if(dir)dir.close();return;}
  File entry=dir.openNextFile();
  while(entry && tracks.size()<Config::MAX_TRACKS) {
    String name=entry.name();
    if(!name.startsWith("/")) {name=path + (path=="/"?"":"/") + name;}
    bool isDir=entry.isDirectory();
    entry.close(); // avoid too many open File handles while descending
    if(isDir) {
      if(!name.startsWith("/System Volume") && !name.startsWith("/iPod_Control"))scanFolder(name,depth+1);
    } else if(isAudio(name)) {
      Track t;t.path=name;t.title=noExt(name);t.folder=parent(name);
      String album=t.folder;int sl=album.lastIndexOf('/');if(sl>=0)album=album.substring(sl+1);
      t.album=album.length()?album:"Unknown Album";
      String artist=parent(t.folder);sl=artist.lastIndexOf('/');if(sl>=0)artist=artist.substring(sl+1);
      t.artist=artist.length()?artist:"Unknown Artist";t.genre="Unknown";
      metadata(t);tracks.push_back(t);
    }
    entry=dir.openNextFile();
  }
  dir.close();
}
void MediaLibrary::metadata(Track &t) {
  String ext=t.path;ext.toLowerCase();if(!ext.endsWith(".mp3"))return;
  File f=SD.open(t.path.c_str());if(!f)return;
  uint8_t head[10];if(f.read(head,10)!=10 || memcmp(head,"ID3",3)!=0){f.close();return;}
  int version=head[3];if(version!=3 && version!=4){f.close();return;}
  uint32_t total=((head[6]&127)<<21)|((head[7]&127)<<14)|((head[8]&127)<<7)|(head[9]&127);
  total=min(total,(uint32_t)65536);
  uint32_t pos=10,end=10+total;
  while(pos+10<=end) {
    uint8_t fh[10];if(f.read(fh,10)!=10)break;
    if(fh[0]==0)break;
    uint32_t n= version==4 ? ((fh[4]&127)<<21)|((fh[5]&127)<<14)|((fh[6]&127)<<7)|(fh[7]&127) : bigEndian4(fh+4);
    pos+=10;if(!n || pos+n>end || n>65536)break;
    String frame=String((char)fh[0])+String((char)fh[1])+String((char)fh[2])+String((char)fh[3]);
    if(frame=="TIT2" || frame=="TPE1" || frame=="TALB" || frame=="TCON") {
      String value=id3Text(f,n);
      if(value.length()) {
        if(frame=="TIT2") t.title=value;
        if(frame=="TPE1") t.artist=value;
        if(frame=="TALB") t.album=value;
        if(frame=="TCON") t.genre=value;
      }
    }
    pos+=n;f.seek(pos);
  }
  f.close();
}
void MediaLibrary::allIndices(std::vector<size_t>& indices) const {
 indices.clear();indices.reserve(tracks.size());for(size_t i=0;i<tracks.size();i++)indices.push_back(i);
}
static String prop(Category cat,const Track &t) {
 switch(cat) {case Category::Artist:return t.artist;case Category::Album:return t.album;
 case Category::Genre:return t.genre;case Category::Folder:return t.folder;
 default:return String();}
}
void MediaLibrary::categories(Category cat,std::vector<String>& out) const {
  out.clear();if(cat==Category::Playlist){readPlaylistNames(out);return;}
  for(const Track &t:tracks){String x=prop(cat,t);if(x.isEmpty())continue;
    bool found=false;for(const auto &v:out)if(v.equalsIgnoreCase(x)){found=true;break;}
    if(!found)out.push_back(x);
  }
  std::sort(out.begin(),out.end(),strLess);
}
void MediaLibrary::matching(Category cat,const String &name,std::vector<size_t>& indices) const {
  indices.clear();
  if(cat==Category::Playlist) {
    String file="/Playlists/"+name;
    if(!SD.exists(file.c_str()))file+=".m3u";
    if(!SD.exists(file.c_str()))file+="8";
    File f=SD.open(file.c_str());if(!f)return;
    String base=parent(file);
    while(f.available()) {
      String line=f.readStringUntil('\n');line.trim();
      if(line.length()==0||line[0]=='#')continue;
      line.replace('\\','/');
      String candidate=line.startsWith("/")?line:base+"/"+line;
      // Canonicalize relative path, permitting ../Music/track.mp3.
      std::vector<String> segments;int pos=0;
      while(pos<candidate.length()) {
        int slash=candidate.indexOf('/',pos);
        if(slash<0)slash=candidate.length();
        String component=candidate.substring(pos,slash);pos=slash+1;
        if(component=="..") {if(!segments.empty())segments.pop_back();}
        else if(component.length()&&component!=".")segments.push_back(component);
      }
      String path;
      for(const auto& segment:segments)path+="/"+segment;
      for(size_t i=0;i<tracks.size();i++)if(tracks[i].path.equalsIgnoreCase(path)){
        indices.push_back(i);break;
      }
    }
    f.close();return;
  }
  for(size_t i=0;i<tracks.size();i++)if(prop(cat,tracks[i]).equalsIgnoreCase(name))indices.push_back(i);
}

void MediaLibrary::readPlaylistNames(std::vector<String>& out)const {
  File dir=SD.open("/Playlists");if(!dir||!dir.isDirectory())return;
  File f=dir.openNextFile();
  while(f){String name=f.name();if(name.startsWith("/"))name=name.substring(name.lastIndexOf('/')+1);
    String lower=name;lower.toLowerCase();if(!f.isDirectory() && (lower.endsWith(".m3u")||lower.endsWith(".m3u8")))out.push_back(name);
    f.close();f=dir.openNextFile();}
  dir.close();std::sort(out.begin(),out.end(),strLess);
}
