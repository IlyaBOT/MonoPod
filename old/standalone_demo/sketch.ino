// Standalone Wokwi demo for MonoPod six-electrode wheel.
// The Wokwi chip outputs 6 DIGITAL pad indications; on physical
// hardware use touchRead() with independent calibration instead.
#include <Arduino.h>
static const uint8_t electrodes[6] = {4,13,14,27,33,32};
static int previous = -1;
static uint32_t lastPoll = 0;
void setup() {
  Serial.begin(115200);
  for (int i=0;i<6;i++) pinMode(electrodes[i], INPUT);
  Serial.println("MonoPod Wokwi Click Wheel: use Angle and Finger touching sliders");
}
void loop() {
  if (millis()-lastPoll<12) return;
  lastPoll=millis();
  int n=0,a=-1,b=-1;
  for (int i=0;i<6;i++) if (digitalRead(electrodes[i])) {
    if(a<0)a=i; else b=i;
    n++;
  }
  int position=-1;
  if(n==1) position=a*2;
  else if(n==2 && b==(a+1)%6) position=2*a+1;
  else if(n==2 && a==0 && b==5) position=11;
  if(position<0){previous=-1;return;}
  if(previous<0){previous=position;return;}
  int d=(position-previous+12)%12;
  if(d>6)d-=12;
  previous=position;
  if(d>3 || d< -3 || !d) return;
  Serial.printf("Scroll %s, steps=%d, position=%d/12\n", d>0?"CW":"CCW",abs(d),position);
}
