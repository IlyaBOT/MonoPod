#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include "../DIY_iPod_Mono/ButtonAdcCompensation.h"
using namespace ButtonAdcCompensation;

static void runVoltages(uint16_t idle, const uint16_t buttonLevels[5]) {
  Ladder ladder;
  // Warm up at released voltage, as the real polling loop does.
  for (unsigned n = 0; n < 16; ++n) ladder.observe(idle);
  assert(ladder.hasIdleSample());
  assert(ladder.decode(idle) == Button::None);
  for (int i = 0; i < 5; ++i) {
    const Button expected[] = {Button::Menu, Button::Previous, Button::Next,
                               Button::PlayPause, Button::Select};
    assert(ladder.decode(buttonLevels[i]) == expected[i]);
    // Sampling a pressed button must not mutate the rail estimate.
    const uint16_t ref = ladder.referenceMilliVolts();
    for (unsigned n = 0; n < 12; ++n) ladder.observe(buttonLevels[i]);
    assert(ladder.referenceMilliVolts() == ref);
  }
  // No unexpected SELECT on high/idle samples, including release transients.
  assert(ladder.decode(3148) == Button::None);
}

int main() {
  // Actual data measured for 3.3V 20k pull-up (released ADC saturates ~3.15V).
  const uint16_t nominal[5]={0,655,1075,1650,2215};
  runVoltages(3148,nominal);

  // 2.9V rail with ideal resistance ratios, as observed in the prototype.
  const uint16_t sag[5]={0,574,945,1450,1946};
  runVoltages(2900,sag);

  // Cold boot at a sagged rail: initially assumes nominal, then learns 2.9V.
  Ladder cold;
  assert(cold.decode(2900)==Button::None);
  cold.observe(2900);
  assert(cold.decode(1946)==Button::Select);
  assert(cold.decode(1450)==Button::PlayPause);

  // Sudden droop during a press: while GPIO9 is pressed we cannot measure VCC,
  // but the margins preserve every key across the observed 3.3V -> 2.9V step.
  Ladder duringPress;
  for(unsigned n=0;n<20;++n) duringPress.observe(3148);
  const Button expected[5]={Button::Menu,Button::Previous,Button::Next,
                            Button::PlayPause,Button::Select};
  for(int i=0;i<5;++i) assert(duringPress.decode(sag[i])==expected[i]);

  // Small ADC read noise around pressed levels should not alter the key ID.
  for(int i=0;i<5;++i){
    for(int j=-25;j<=25;j+=5){
      const int sample=(int)sag[i]+j;
      if(sample>=0) assert(cold.decode((uint16_t)sample)==expected[i]);
    }
  }

  // Rail recovers while all buttons are released. It should follow swiftly.
  for (unsigned n = 0; n < 24; ++n) cold.observe(3148);
  assert(cold.referenceMilliVolts() >= 3100);
  assert(cold.decode(2215)==Button::Select);

  // Deliberate dead zones between button resistance ratios.
  Ladder l;
  for(unsigned n=0;n<10;++n) l.observe(3150);
  assert(l.decode(865)==Button::None); // between PREV and NEXT
  assert(l.decode(1325)==Button::None); // between NEXT and PLAY
  assert(l.decode(1835)==Button::None); // between PLAY and SELECT
  assert(l.decode(2430)==Button::None); // above SELECT band
  // Invalid 3V3 measurement while a button is held cannot enter idle tracker.
  auto v=l.referenceMilliVolts();
  for(unsigned n=0;n<50;++n) l.observe(2215);
  assert(l.referenceMilliVolts()==v);

  puts("Button ADC supply-compensation tests passed");
  return 0;
}
