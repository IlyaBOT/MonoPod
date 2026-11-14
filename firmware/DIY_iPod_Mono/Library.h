#pragma once
#include "Types.h"
#include <SD.h>
#include <vector>
class MediaLibrary {
public:
  bool begin(bool scanOnMount=true);
  void rescan(bool verifyFingerprints=false);
  void markDirty(const String& path="/",uint32_t fullCRC=0);
  bool dirty()const{return indexDirty;}
  uint32_t generation()const{return indexGeneration;}
  bool indexReady()const{return indexReady_;}
  void eject();
  bool remount(){return begin();}
  bool formatCard();  // guarded: never formats a different drive accidentally
  size_t size() const { return tracks.size(); }
  const Track& at(size_t i) const { return tracks.at(i); }
  const std::vector<Track>& all() const {return tracks;}
  bool sdOK() const {return mounted;}
  uint32_t mountedFrequency() const {return activeSpiHz;}
  uint64_t usedBytes() const {return mounted?SD.usedBytes():0;}
  uint64_t totalBytes() const {return mounted?SD.totalBytes():0;}
  void categories(Category type,std::vector<String>& out) const;
  void matching(Category type,const String &name,std::vector<size_t>& indices) const;
  void allIndices(std::vector<size_t>& indices) const;
  String lastError() const {return error;}
  static bool isAudio(const String &name);
private:
  std::vector<Track> tracks;
  bool mounted=false;
  bool indexDirty=true,indexReady_=false;
  uint32_t indexGeneration=0;
  std::vector<Track> oldIndex;
  std::vector<String> dirtyPaths;
  std::vector<uint32_t> dirtyCRCs;
  bool verifyFingerprintsNow=false,indexChanged=false;
  unsigned reused=0,parsed=0;
  uint32_t activeSpiHz=0;
  String error;
  void scanFolder(const String &path,uint8_t depth);
  void readPlaylistNames(std::vector<String>& out)const;
  void metadata(Track &track);
  static String parent(const String &path);
};
extern MediaLibrary media;
