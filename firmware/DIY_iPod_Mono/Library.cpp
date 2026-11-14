#include "Library.h"
#include "Config.h"
#include <SPI.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include "Id3Parser.h"
#if FEATURE_SD_FORMAT
#define DISABLE_FS_H_WARNING
#include <SdFat.h>
#endif
MediaLibrary media;
static String clipTag(String s) {s.trim(); if(s.length()>80) s.remove(80);return s;}
static String noExt(String n) {int slash=n.lastIndexOf('/');if(slash>=0)n=n.substring(slash+1);int dot=n.lastIndexOf('.');if(dot>0)n.remove(dot);return n;}
static bool strLess(String a,String b) {a.toLowerCase(); b.toLowerCase();return a.compareTo(b)<0;}
// Xing/Info frame fields use network (big-endian) byte order.
static uint32_t bigEndian4(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) |
         (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8)  |
          uint32_t(p[3]);
}
// MP3 duration: exact when Xing/Info frame count is available; otherwise CBR estimate.
// Accurate remaining time needs TLEN/Xing. Unknown files display --:-- instead.
static uint32_t probeMp3Duration(const String& path){
  File f=SD.open(path.c_str());if(!f)return 0;
  uint32_t start=0;
  uint8_t id3[10];
  if(f.read(id3,10)==10 && memcmp(id3,"ID3",3)==0){
    start=10UL+((uint32_t)(id3[6]&127)<<21)+((uint32_t)(id3[7]&127)<<14)+((uint32_t)(id3[8]&127)<<7)+(id3[9]&127);
  }
  if(start>=f.size()){f.close();return 0;}
  f.seek(start);
  // This probe is inlined into recursive scanFolder by -Os. A 4 KiB local
  // array made EACH recursive frame 4416 bytes and overflowed loopTask's
  // 8192-byte stack before TFT initialization. Keep the bounded window on
  // the heap; allocation failure leaves duration unknown without failing boot.
  constexpr size_t probeBytes=4096;
  std::unique_ptr<uint8_t[]> b(new(std::nothrow) uint8_t[probeBytes]);
  if(!b){f.close();return 0;}
  int n=f.read(b.get(),probeBytes);
  uint32_t secs=0;
  for(int i=0;i+4<n;i++){
    if(b[i]!=0xff || (b[i+1]&0xe0)!=0xe0)continue;
    int version=(b[i+1]>>3)&3,layer=(b[i+1]>>1)&3;
    int bitrateIndex=(b[i+2]>>4)&15,sampleIndex=(b[i+2]>>2)&3;
    if(version==1 || layer!=1 || bitrateIndex==0 || bitrateIndex==15 || sampleIndex==3)continue;
    static const uint16_t rates1[]={0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
    static const uint16_t rates2[]={0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0};
    const int rate= (version==3?rates1[bitrateIndex]:rates2[bitrateIndex]);
    const int base[3]={44100,48000,32000};
    const int sampleRate=base[sampleIndex]>>(version==3?0:(version==2?1:2));
    const int samplesPerFrame=(version==3?1152:576);
    if(!rate||!sampleRate)continue;
    // MPEG layer III Xing/Info frame count, including optional CRC and side information.
    const int mono=((b[i+3]>>6)&3)==3;
    const int side=(version==3?(mono?17:32):(mono?9:17));
    const int off=i+4+((b[i+1]&1)?0:2)+side;
    if(off+12<n && (memcmp(b.get()+off,"Xing",4)==0 || memcmp(b.get()+off,"Info",4)==0)){
      const uint32_t flags=bigEndian4(b.get()+off+4);
      if(flags&1){const uint32_t frames=bigEndian4(b.get()+off+8);
        if(frames)secs=(uint32_t)(((uint64_t)frames*samplesPerFrame+sampleRate/2)/sampleRate);
      }
    }
    if(!secs){uint32_t bytes=f.size()-(start+(uint32_t)i);
      secs=(uint32_t)(((uint64_t)bytes*8+500UL*rate)/(1000UL*rate));
    }
    break;
  }
  f.close();return secs;
}
bool MediaLibrary::isAudio(const String &name) {
  String n=name; n.toLowerCase();
  return n.endsWith(".mp3") || n.endsWith(".wav") || n.endsWith(".flac") || n.endsWith(".aac");
}
String MediaLibrary::parent(const String &p) {int at=p.lastIndexOf('/');return at>0 ? p.substring(0,at) : String("/");}
bool MediaLibrary::begin(bool scanOnMount) {
  if(mounted)eject();
  // SD must enter SPI mode BEFORE the TFT communicates on their shared bus.
  pinMode(Config::PIN_TFT_CS,OUTPUT);digitalWrite(Config::PIN_TFT_CS,HIGH);
  pinMode(Config::PIN_SD_CS,OUTPUT);digitalWrite(Config::PIN_SD_CS,HIGH);
  activeSpiHz=0;
  // Hardware wiring confirmed; mount once at the configured SPI rate.
  // Keep the SD-before-TFT initialization order on the shared bus.
  mounted=SD.begin(Config::PIN_SD_CS,SPI,Config::SPI_SD_HZ);
  if(mounted){
    activeSpiHz=Config::SPI_SD_HZ;
    error="";
    MONOPOD_LOG.printf("SD: OK type=%u size=%llu B SPI=%lu Hz\n",
      (unsigned)SD.cardType(),(unsigned long long)SD.cardSize(),
      (unsigned long)activeSpiHz);
    if(scanOnMount)rescan();
    return true;
  }
  SD.end();
  error="microSD mount failed (check CS GPIO, MISO, wiring and filesystem)";
  return false;
}
void MediaLibrary::rescan() {
  tracks.clear();if(!mounted)return;
  // Large contiguous Track storage prefers PSRAM via the ESP32 heap allocator.
  // Allocate once, avoiding transient vector-growth copies during long scans.
#if defined(ARDUINO_ARCH_ESP32)
  if(psramFound() && tracks.capacity()<Config::MAX_TRACKS)
    tracks.reserve(Config::MAX_TRACKS);
#endif
  error="";scanFolder("/Music",0);
  if(tracks.empty()) scanFolder("/",0);
  std::sort(tracks.begin(),tracks.end(),[](const Track &a,const Track &b){return strLess(a.title,b.title);});
  if(tracks.size()>=Config::MAX_TRACKS) error="Library limit reached";
  // The index owns String metadata; actual RAM use depends on filename/ID3 lengths.
  // Report heap state so 1024+ track builds can be validated on real hardware.
#if defined(ARDUINO_ARCH_ESP32)
  MONOPOD_LOG.printf("Library: %u/%u tracks, heap free=%u B, PSRAM free=%u B\n",
    (unsigned)tracks.size(),(unsigned)Config::MAX_TRACKS,
    (unsigned)ESP.getFreeHeap(),(unsigned)ESP.getFreePsram());
#endif
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
      t.album="Unknown Album";
      t.artist="Unknown Artist";t.genre="Unknown Genre";
      metadata(t);
      if(t.durationSeconds==0 && (name.endsWith(".mp3") || name.endsWith(".MP3")))t.durationSeconds=probeMp3Duration(name);
      // Read canonical PCM WAV header (RIFF/WAVE, fmt/data). Do not estimate MP3 VBR duration.
      if(name.endsWith(".wav") || name.endsWith(".WAV")){
        File wf=SD.open(name.c_str()); uint8_t wh[44];
        if(wf && wf.read(wh,sizeof(wh))==sizeof(wh) && memcmp(wh,"RIFF",4)==0 && memcmp(wh+8,"WAVE",4)==0 && memcmp(wh+12,"fmt ",4)==0){
          uint32_t bytesPerSecond=(uint32_t)wh[28]|((uint32_t)wh[29]<<8)|((uint32_t)wh[30]<<16)|((uint32_t)wh[31]<<24);
          if(bytesPerSecond && memcmp(wh+36,"data",4)==0){uint32_t dataBytes=(uint32_t)wh[40]|((uint32_t)wh[41]<<8)|((uint32_t)wh[42]<<16)|((uint32_t)wh[43]<<24);t.durationSeconds=(dataBytes+bytesPerSecond/2)/bytesPerSecond;}
        }
        if(wf)wf.close();
      }
      tracks.push_back(t);
    }
    entry=dir.openNextFile();
  }
  dir.close();
}
void MediaLibrary::metadata(Track &t) {
  String ext=t.path;ext.toLowerCase();if(!ext.endsWith(".mp3"))return;
  File f=SD.open(t.path.c_str());if(!f)return;
  Id3Parser::Tags tags;
  if(Id3Parser::parse(f,tags)){
    if(tags.title[0])t.title=tags.title;
    if(tags.artist[0])t.artist=tags.artist;
    if(tags.album[0])t.album=tags.album;
    if(tags.genre[0])t.genre=tags.genre;
    if(tags.year)t.year=tags.year;
    if(tags.durationSeconds)t.durationSeconds=tags.durationSeconds;
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

void MediaLibrary::eject(){
  tracks.clear();
  if(mounted){SD.end();mounted=false;}
  activeSpiHz=0;
  error="Card safely ejected";
}
bool MediaLibrary::formatCard(){
#if FEATURE_SD_FORMAT
  // Explicit, double-confirmed DESTRUCTIVE operation initiated only from the UI.
  // The S3 SD.h interface supports FAT; choose FAT16/32 for compatibility with
  // the playback and USB sync filesystem. Do not create an unmountable exFAT card.
  eject();
  pinMode(Config::PIN_TFT_CS, OUTPUT); digitalWrite(Config::PIN_TFT_CS,HIGH);
  pinMode(Config::PIN_SD_CS, OUTPUT); digitalWrite(Config::PIN_SD_CS,HIGH);
  {
    SdCardFactory factory;
    SdCard* card=factory.newCard(SdSpiConfig(Config::PIN_SD_CS,SHARED_SPI,
                                             SD_SCK_MHZ(4),&SPI));
    if(!card || card->errorCode() || !card->sectorCount()){
      error="Formatter: SD card init failed";return false;
    }
    uint8_t sectorBuffer[512] __attribute__((aligned(4)))={};
    // SdFat FatFormatter chooses FAT16/FAT32 based on sector count.
    // FAT32 on SDXC (>32 GiB) is intentional for the Arduino SDFS backend.
    FatFormatter formatter;
    MONOPOD_LOG.println("SD FORMAT: destroying all existing files, creating FAT volume");
    if(!formatter.format(card,sectorBuffer,&MONOPOD_LOG)){
      error="Formatter: FAT creation failed";return false;
    }
  }
  // The formatting card object has now gone out of scope; mount normally.
  if(!begin()){
    error="FAT written, but SD mount failed";return false;
  }
  if(!SD.exists("/Music") && !SD.mkdir("/Music")){
    error="Formatted, failed to create /Music";return false;
  }
  if(!SD.exists("/Playlists") && !SD.mkdir("/Playlists")){
    error="Formatted, failed to create /Playlists";return false;
  }
  rescan();
  MONOPOD_LOG.println("SD FORMAT: FAT volume ready, /Music and /Playlists created");
  return true;
#else
  error="SD formatting unavailable (SdFat v2 not compiled)";
  return false;
#endif
}

