#include "ClickMixOutput.h"
#include <assert.h>
#include <stdio.h>
struct FakeOutput:AudioOutput{
 int started=0, stopped=0, consumed=0;
 bool SetRate(int) override{return true;}
 bool SetChannels(int) override{return true;}
 bool SetGain(float) override{return true;}
 bool begin() override{started++;return true;}
 bool ConsumeSample(int16_t s[2]) override{(void)s;consumed++;return true;}
 bool stop() override{stopped++;return true;}
};
int main(){
 FakeOutput target;
 ClickMixOutput m(&target);
 assert(m.SetRate(16000));assert(m.SetChannels(2));
 m.trigger(2100,3); // 48 frames of short, broadband crackle
 for(int i=0;i<5;i++)m.serviceIdle(128);
 assert(target.consumed>=48);
 assert(target.consumed<=48+256);
 assert(target.stopped==1); // idle I2S must not loop the last DMA frames
 m.trigger(2100,3);
 for(int i=0;i<5;i++)m.serviceIdle(128);
 assert(target.stopped==2);assert(target.started==2);
 m.cancelClick();
 puts("click_mixer_test: OK");
}
