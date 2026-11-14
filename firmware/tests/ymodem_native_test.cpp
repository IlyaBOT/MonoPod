#include "../MonoPod_S3_DevTest/YModemRx.h"
#include <assert.h>
#include <stdio.h>
#include <vector>
#include <string>
using namespace YModem;
struct Context {std::vector<uint8_t> bytes;size_t size=0;int done=0, abort=0;};
static bool begin(void* c,const char* name,uint32_t n){Context& x=*(Context*)c;assert(std::string(name)=="test.pcm");x.size=n;return true;}
static bool write(void* c,const uint8_t* buf,size_t n){Context& x=*(Context*)c;x.bytes.insert(x.bytes.end(),buf,buf+n);return true;}
static void done(void* c){((Context*)c)->done++;}
static void aborted(void* c){((Context*)c)->abort++;}
std::vector<uint8_t> packet(uint8_t type,uint8_t seq,const std::vector<uint8_t>& payload){
 size_t n=type==SOH?128:1024;std::vector<uint8_t> p={type,seq,(uint8_t)~seq};
 p.insert(p.end(),payload.begin(),payload.end());p.resize(n+3,0x1a);
 auto crc=crc16(p.data()+3,n);p.push_back(crc>>8);p.push_back(crc&255);return p;
}
Action send(Receiver& r,const std::vector<uint8_t>& p,int& clock){Action result;for(auto b:p) {Action a=r.input(b,++clock);if(a.length)result=a;}return result;}
int main(){
 Context c;Receiver r({&c,begin,write,done,aborted});r.start(0);
 assert(r.greeting().response[0]=='C');int t=0;
 std::vector<uint8_t> header={'t','e','s','t','.','p','c','m',0,'2','0','5','1',0};
 Action h=send(r,packet(SOH,0,header),t);assert(h.length==2&&h.response[0]==ACK&&h.response[1]=='C');
 std::vector<uint8_t> a(1024),b(1024);for(int i=0;i<1024;i++){a[i]=i&255;b[i]=(i+17)&255;}
 auto bad=packet(STX,1,a);bad.back()^=0x80;
 assert(send(r,bad,t).response[0]==NAK);
 assert(r.stats().crcErrors==1);
 auto good=packet(STX,1,a);
 assert(send(r,good,t).response[0]==ACK);
 assert(send(r,good,t).response[0]==ACK); // duplicate must NOT append a second time
 assert(send(r,packet(STX,2,b),t).response[0]==ACK);
 assert(send(r,packet(SOH,3,{72,73,74}),t).response[0]==ACK);
 assert(r.bytes()==2051);
 assert(r.input(EOT,++t).response[0]==NAK);
 Action e=r.input(EOT,++t);assert(e.length==2&&e.response[0]==ACK&&e.response[1]=='C');
 Action last=send(r,packet(SOH,0,{0}),t);assert(last.finished&&last.response[0]==ACK);
 assert(c.done==1&&c.abort==0&&c.bytes.size()==2051&&c.bytes[2048]==72);
 assert(r.state()==State::Complete);
 assert(r.stats().crcErrors==1 && r.stats().packetsOk==5);
 // The new conservative uploader uses 128-byte YMODEM data blocks.
 Context cs; Receiver rs({&cs,begin,write,done,aborted}); rs.start(0);
 Action hs=send(rs,packet(SOH,0,header),t);assert(hs.length==2);
 std::vector<uint8_t> little(128,0x47);
 assert(send(rs,packet(SOH,1,little),t).response[0]==ACK);
 assert(send(rs,packet(SOH,2,little),t).response[0]==ACK);
 assert(send(rs,packet(SOH,3,little),t).response[0]==ACK);
 assert(rs.bytes()==384);
 // Expected file length is 2051, so all data must be supplied before EOT.
 for(int seq=4;seq<=17;seq++) assert(send(rs,packet(SOH,(uint8_t)seq,little),t).response[0]==ACK);
 assert(rs.bytes()==2051);
 assert(rs.input(EOT,++t).response[0]==NAK);
 assert(rs.input(EOT,++t).response[0]==ACK);
 assert(send(rs,packet(SOH,0,{0}),t).finished);
 assert(cs.bytes.size()==2051 && cs.done==1);
 // Incomplete frame: timeout must count as a diagnostic rejection.
 Context ci; Receiver ri({&ci,begin,write,done,aborted}); ri.start(0);
 assert(send(ri,packet(SOH,0,header),t).length==2);
 ri.input(STX,100); ri.input(1,101);
 assert(ri.tick(2700).response[0]==NAK);
 assert(ri.stats().timeouts==1);
 // Malformed transfer: no well-formed size should not allocate or write.
 Context badCtx;Receiver r2({&badCtx,begin,write,done,aborted});r2.start(0);
 auto wrong=send(r2,packet(SOH,0,{'t','e','s','t','.','p','c','m',0,'x'}),t);
 assert(wrong.error && badCtx.abort==1);
 puts("YMODEM native tests: PASS (CRC, retransmission, size padding, EOT, failure)");
}
