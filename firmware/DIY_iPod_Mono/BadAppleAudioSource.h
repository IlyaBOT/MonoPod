#pragma once
// Read-only MP3 window inside a BAV2 file. Uses a second SD handle, so video
// seeking and the decoder's read position cannot interfere with one another.
#include <AudioFileSource.h>
#include <SD.h>
#include <stdio.h>
class BadAppleAudioSource final : public AudioFileSource {
public:
  BadAppleAudioSource(const char* name,uint32_t base,uint32_t length)
      : start_(base),size_(length) {
    file_=SD.open(name,FILE_READ);
    if(!file_ || (uint64_t)base+length>file_.size() || !file_.seek(base)) close();
  }
  ~BadAppleAudioSource() override {close();}
  uint32_t read(void* data,uint32_t len) override {
    if(!file_)return 0;
    uint32_t n=min(len,size_-cursor_);
    n=(uint32_t)file_.read((uint8_t*)data,n);
    cursor_+=n;return n;
  }
  bool seek(int32_t offset,int dir) override {
    int64_t p=dir==SEEK_SET?(int64_t)offset:
      dir==SEEK_CUR?(int64_t)cursor_+offset:
      dir==SEEK_END?(int64_t)size_+offset:-1;
    if(!file_ || p<0 || p>(int64_t)size_)return false;
    if(!file_.seek(start_+(uint32_t)p))return false;
    cursor_=(uint32_t)p;return true;
  }
  bool close() override {if(file_)file_.close();return true;}
  bool isOpen() override {return (bool)file_;}
  uint32_t getSize() override {return size_;}
  uint32_t getPos() override {return cursor_;}
private:
  File file_;
  uint32_t start_=0,size_=0,cursor_=0;
};
