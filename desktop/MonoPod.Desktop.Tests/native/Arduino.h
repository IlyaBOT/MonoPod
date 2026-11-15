#pragma once
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <chrono>
#include <cctype>
using std::min;
class String:public std::string {
public:using std::string::string;String(const std::string& s):std::string(s){}String(unsigned long long n):std::string(std::to_string(n)){}String(unsigned long n):std::string(std::to_string(n)){}String(unsigned n):std::string(std::to_string(n)){}String(int n):std::string(std::to_string(n)){}String(long n):std::string(std::to_string(n)){}
 bool isEmpty()const{return empty();}bool startsWith(const String& s)const{return rfind(s,0)==0;}bool endsWith(const String& s)const{return size()>=s.size()&&compare(size()-s.size(),s.size(),s)==0;}int indexOf(char c,int start=0)const{auto p=find(c,start);return p==npos?-1:(int)p;}int lastIndexOf(char c)const{auto p=rfind(c);return p==npos?-1:(int)p;}String substring(int from,int to=-1)const{if(from>(int)size())return "";return substr(from,to<0?npos:to-from);}void toLowerCase(){for(char& c:*this)c=(char)tolower((unsigned char)c);}void trim(){auto a=find_first_not_of(" \t\r\n"),b=find_last_not_of(" \t\r\n");*this=a==npos?"":substr(a,b-a+1);}
};
inline uint32_t millis(){return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}inline void yield(){}

#include <deque>
struct TestSerial {
 std::deque<uint8_t> input;std::string output;
 int available(){return (int)input.size();}int read(){if(input.empty())return -1;int v=input.front();input.pop_front();return v;}
 void print(const String& s){output+=s;}void println(const String& s){output+=s;output+='\n';}size_t write(const uint8_t* b,size_t n){output.append((const char*)b,n);return n;}explicit operator bool()const{return true;}
};extern TestSerial Serial;
