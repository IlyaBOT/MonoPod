#include "Library.h"
#include <iostream>
#include <stdexcept>
SDAdapter SD;
void check(bool b,const char* msg){if(!b)throw std::runtime_error(msg);}
void save(fs::path p,const std::vector<uint8_t>& b){fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);f.write((const char*)b.data(),b.size());}
void be32(std::vector<uint8_t>& b,size_t at,uint32_t n){for(int i=0;i<4;i++)b[at+i]=(uint8_t)(n>>(24-8*i));}
void le32(std::vector<uint8_t>& b,size_t at,uint32_t n){for(int i=0;i<4;i++)b[at+i]=(uint8_t)(n>>(8*i));}
int main(int argc,char**argv){try{
SD.root=argv[1];fs::path where=SD.root/"Music";
std::vector<uint8_t> mp3(4096);mp3[0]=255;mp3[1]=251;mp3[2]=144;mp3[3]=192;
memcpy(mp3.data()+21,"Xing",4);be32(mp3,25,1);be32(mp3,29,1000);
for(int depth=0;depth<=7;depth++){
 save(where/("track"+std::to_string(depth)+".mp3"),mp3);
 where/="nested";
}
std::vector<uint8_t> wav(44+88200);memcpy(wav.data(),"RIFF",4);memcpy(wav.data()+8,"WAVEfmt ",8);
le32(wav,28,88200);memcpy(wav.data()+36,"data",4);le32(wav,40,88200);save(SD.root/"Music"/"sample.wav",wav);
save(SD.root/"Music"/"empty.mp3",{});
check(media.begin(),"mount");
check(media.size()==9,"depth limit or track count");
int validMp3=0,waves=0,unknown=0;
for(const auto& t:media.all()){
 if(t.path.endsWith(".wav")){check(t.durationSeconds==1,"WAV duration");++waves;}
 else if(t.path.endsWith("empty.mp3")){check(t.durationSeconds==0,"unknown duration");++unknown;}
 else {check(t.durationSeconds==26,"Xing duration");++validMp3;}
 check(t.path.find("track7.mp3")==String::npos,"scanned beyond depth limit");
}
check(validMp3==7&&waves==1&&unknown==1,"case counts");
media.rescan();check(media.size()==9,"rescan duplicated files");
media.eject();check(!media.sdOK()&&media.size()==0,"eject");
std::cout<<"PASS actual Library.cpp: recursive SD directories through depth 6, reject depth 7, MP3 Xing, WAV, empty/unknown and rescan/eject\n";
return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
