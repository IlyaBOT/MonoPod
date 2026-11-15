#pragma once
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <cctype>
using std::min;
class String:public std::string {
public:
using std::string::string;
String(const std::string& s):std::string(s){}
String(char c):std::string(1,c){}
String(int n):std::string(std::to_string(n)){}
String(unsigned n):std::string(std::to_string(n)){}
String(unsigned long n):std::string(std::to_string(n)){}
bool isEmpty()const{return empty();}
bool startsWith(const String& s)const{return rfind(s,0)==0;}
bool endsWith(const String& s)const{return size()>=s.size()&&compare(size()-s.size(),s.size(),s)==0;}
int indexOf(char c,int start=0)const{auto p=find(c,start);return p==npos?-1:(int)p;}
int lastIndexOf(char c)const{auto p=rfind(c);return p==npos?-1:(int)p;}
String substring(int from,int to=-1)const{if(from>(int)size())return "";return substr(from,to<0?npos:to-from);}
void remove(unsigned from){erase(from);}
void toLowerCase(){for(char& c:*this)c=(char)tolower((unsigned char)c);}
bool equalsIgnoreCase(String s)const{String a=*this;a.toLowerCase();s.toLowerCase();return a==s;}
int compareTo(const String& s)const{return compare(s);}
void replace(char a,char b){std::replace(begin(),end(),a,b);}
void trim(){auto a=find_first_not_of(" \t\r\n"),b=find_last_not_of(" \t\r\n");*this=a==npos?"":substr(a,b-a+1);}
};
inline void pinMode(int,int){} inline void digitalWrite(int,int){}
#define OUTPUT 1
#define HIGH 1
struct Log {template<class... T> void printf(const char*,T...){} template<class T> void println(T){} };
inline Log testLog;
#define MONOPOD_LOG testLog
