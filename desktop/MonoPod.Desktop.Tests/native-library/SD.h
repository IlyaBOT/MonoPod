#pragma once
#include "Arduino.h"
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>
#define FILE_READ 0
namespace fs=std::filesystem;
class File {
struct State {fs::path path;std::ifstream stream;bool directory=false,valid=false;std::vector<fs::path> entries;size_t cursor=0;};
std::shared_ptr<State> s;
public:
File()=default;
File(fs::path p):s(std::make_shared<State>()){
s->path=p;s->directory=fs::is_directory(p);if(s->directory){s->valid=true;for(auto& e:fs::directory_iterator(p))s->entries.push_back(e.path());}else{s->stream.open(p,std::ios::binary);s->valid=s->stream.is_open();}}
explicit operator bool()const{return s&&s->valid;}
bool isDirectory()const{return s&&s->directory;}
size_t size()const{return *this&&!s->directory?(size_t)fs::file_size(s->path):0;}
const char* name()const{static std::string n;n=s?s->path.filename().string():"";return n.c_str();}
File openNextFile(){return s&&s->cursor<s->entries.size()?File(s->entries[s->cursor++]):File();}
int read(uint8_t* b,size_t n){s->stream.read((char*)b,n);return (int)s->stream.gcount();}
int read(){return s->stream.get();}
bool seek(size_t n){s->stream.clear();s->stream.seekg(n);return s->stream.good();}
bool available(){return s&&s->stream.peek()!=EOF;}
String readStringUntil(char c){std::string line;std::getline(s->stream,line,c);return line;}
void close(){if(s){s->stream.close();s->valid=false;}}
};
struct SDAdapter {
fs::path root;
fs::path path(const char* p){return root/fs::path(p[0]=='/'?p+1:p);}
File open(const char* p,int=FILE_READ){return File(path(p));}
bool begin(int,int,uint32_t){return fs::is_directory(root);}
void end(){}
bool exists(const char* p){return fs::exists(path(p));}
bool mkdir(const char* p){return fs::create_directory(path(p));}
int cardType(){return 3;}
uint64_t cardSize(){return 64000000000ULL;}
uint64_t totalBytes(){return cardSize();}
uint64_t usedBytes(){return 1024;}
};
extern SDAdapter SD;
