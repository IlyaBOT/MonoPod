#pragma once
#include <fstream>
#include <string>
#include <cstdint>
#define FILE_READ 0
class File {
public:
  explicit File(const char* name):stream(name,std::ios::binary){}
  File(File&&)=default;
  explicit operator bool()const{return stream.good();}
  size_t size(){auto p=stream.tellg();stream.seekg(0,std::ios::end);auto n=stream.tellg();stream.seekg(p);return size_t(n);}
  int read(uint8_t* b,size_t n){stream.read((char*)b,n);return (int)stream.gcount();}
  bool seek(size_t n){stream.clear();stream.seekg(n);return stream.good();}
  void close(){stream.close();}
private:
  std::ifstream stream;
};
struct SDAdapter { File open(const char* p,int){return File(p);} };
extern SDAdapter SD;
