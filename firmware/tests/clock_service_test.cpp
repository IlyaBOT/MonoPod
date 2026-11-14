#include "ClockService.h"
#include <iostream>
#include <stdexcept>

int main(){
  unsigned checks=0;
  auto check=[&](bool passed){++checks;if(!passed)throw std::runtime_error("clock service behavior failed");};
  try{
    ClockNative::reset();
    ClockService service;service.begin();
    check(!service.ntpEnabled()&&service.ntpServer()=="pool.ntp.org"&&service.use24Hour());
    check(service.set24Hour(false)&&!service.use24Hour()&&!ClockNative::hour24);
    check(service.set24Hour(true)&&service.use24Hour()&&ClockNative::hour24);
    service.loop();check(ClockNative::starts==0);
    check(service.setNtpEnabled(true)&&ClockNative::persistedNtp);
    service.loop();check(ClockNative::starts==0); // offline/AP has no NTP
    ClockNative::online=true;service.loop();
    check(ClockNative::starts==1&&ClockNative::server1=="pool.ntp.org"&&ClockNative::server2=="time.google.com");
    service.loop();check(ClockNative::starts==1);
    ClockNative::onStop=[&](){check(service.ntpServer()=="pool.ntp.org");};
    check(service.setNtpServer("ntp.example.org")&&!ClockNative::sntp);
    ClockNative::onStop={};
    service.loop();check(ClockNative::starts==2&&ClockNative::server1=="ntp.example.org"&&ClockNative::server2.empty());
    check(service.setNtpServer("ntp.example.org"));service.loop();check(ClockNative::starts==2);
    check(service.setTimezone("UTC-5:30")&&!ClockNative::sntp);
    service.loop();check(ClockNative::starts==3&&ClockNative::timezone=="UTC-5:30");
    ClockNative::writable=false;
    check(!service.setNtpServer("other.example.org")&&service.ntpServer()=="ntp.example.org");
    check(!service.setNtpEnabled(false)&&service.ntpEnabled());
    check(!service.setTimezone("UTC0")&&service.timezone()=="UTC-5:30");
    ClockNative::writable=true;
    ClockNative::online=false;service.loop();check(!ClockNative::sntp);
    ClockNative::online=true;service.loop();check(ClockNative::starts==4);
    check(service.setNtpEnabled(false)&&!ClockNative::sntp&&!ClockNative::persistedNtp);
    ClockService rebooted;rebooted.begin();
    check(rebooted.ntpServer()=="ntp.example.org"&&rebooted.timezone()=="UTC-5:30"&&!rebooted.ntpEnabled()&&rebooted.use24Hour());
    check(!rebooted.setNtpServer("http://bad")&&rebooted.ntpServer()=="ntp.example.org");
    service.setUnix(1);check(ClockNative::epoch==0);
    service.setUnix((time_t)1793500000);check(ClockNative::epoch==1793500000&&service.valid());
    service.setUnix((time_t)4102444801LL);check(ClockNative::epoch==1793500000);
    std::cout<<"PASS actual ClockService.cpp with host NVS/Wi-Fi/SNTP/time adapters: "<<checks<<" cases\n";
    return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<" at case "<<checks<<"\n";return 1;}
}
