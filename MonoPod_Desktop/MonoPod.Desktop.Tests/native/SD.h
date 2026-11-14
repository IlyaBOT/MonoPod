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
