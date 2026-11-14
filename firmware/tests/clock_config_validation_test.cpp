#include "ClockConfigValidation.h"
#include <iostream>
#include <stdexcept>
#include <string>

int main(){
  unsigned checks=0;
  auto check=[&](bool passed){++checks;if(!passed)throw std::runtime_error("clock validation case failed");};
  try{
    for(const char* value:{"pool.ntp.org","time.google.com","192.168.1.1","ntp.local","ntp-server.example.org"})
      check(ClockConfigValidation::ntpServer(value));
    for(const char* value:{"","http://pool.ntp.org","ntp.org:123",".ntp.org","ntp.org.","-ntp.org","ntp-.org","one..two","ntp.org/path","ntp.org\n","ntp org"})
      check(!ClockConfigValidation::ntpServer(value));
    std::string label(63,'a');
    std::string longest=label+"."+label+"."+label+"."+std::string(61,'b');
    check(longest.size()==253&&ClockConfigValidation::ntpServer(longest.c_str()));
    check(!ClockConfigValidation::ntpServer((longest+"c").c_str()));
    check(!ClockConfigValidation::ntpServer((label+"a.org").c_str()));
    check(!ClockConfigValidation::ntpServer(nullptr));
    for(const char* value:{"UTC0","UTC-3","UTC-5:30","UTC5DST4,M3.2.0/2,M11.1.0/2","<+03>-3"})
      check(ClockConfigValidation::timezone(value));
    for(const char* value:{"","UTC\n","UTC 3","UTC;reboot","UTC\""})check(!ClockConfigValidation::timezone(value));
    check(!ClockConfigValidation::timezone(nullptr));
    check(!ClockConfigValidation::timezone(std::string(64,'A').c_str()));
    std::cout<<"PASS actual ClockConfigValidation.h: "<<checks<<" cases\n";
    return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<" at case "<<checks<<"\n";return 1;}
}
