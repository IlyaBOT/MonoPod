#include "Backlight.h"
#include "BacklightMath.h"
#include "Config.h"
BacklightController backlight;

void BacklightController::begin(uint8_t pct){
  configured=constrain((int)pct,5,100);
  desiredOn=false; current=from=target=0;moving=false;
#if !WOKWI_SIMULATION
  if(Config::PIN_TFT_BL >= 0){
    const uint8_t pin=(uint8_t)Config::PIN_TFT_BL;
    // Keep a P-MOSFET off while GPIO is starting up.
    digitalWrite(pin,Config::TFT_BL_ACTIVE_LOW?HIGH:LOW);
    pinMode(pin,OUTPUT);
    ready=ledcAttach(pin,Config::TFT_BL_PWM_HZ,Config::TFT_BL_PWM_BITS);
    if(ready)write(0);
    else MONOPOD_LOG.println("Backlight: LEDC attach failed");
  }
#endif
}
void BacklightController::write(uint8_t percent){
#if !WOKWI_SIMULATION
  if(ready){
    const uint16_t duty=BacklightMath::gammaDuty(percent);
    const uint16_t maxDuty=(1U<<Config::TFT_BL_PWM_BITS)-1;
    ledcWrite((uint8_t)Config::PIN_TFT_BL,Config::TFT_BL_ACTIVE_LOW?maxDuty-duty:duty);
  }
#else
  (void)percent;
#endif
}
void BacklightController::request(bool on){
  const uint8_t next=on?configured:0;
  if(desiredOn==on && target==next)return;
  desiredOn=on;from=current;target=next;
  began=millis();lastStep=0;
  fadeMs=on?Config::TFT_BL_FADE_ON_MS:Config::TFT_BL_FADE_OFF_MS;
  moving=(from!=target);
  if(!moving)write(target);
}
void BacklightController::setBrightness(uint8_t pct){
  configured=constrain((int)pct,5,100);
  if(desiredOn)request(true);
}
void BacklightController::wake(){request(true);}
void BacklightController::sleep(){request(false);}
void BacklightController::loop(){
  if(!moving)return;
  const uint32_t now=millis();
  const uint32_t elapsed=now-began;
  if(elapsed < fadeMs && now-lastStep < 10)return;
  lastStep=now;
  const uint16_t eased=BacklightMath::ease(elapsed,fadeMs);
  const uint8_t value=BacklightMath::mix(from,target,eased);
  if(current!=value){current=value;write(current);}
  if(elapsed >= fadeMs){moving=false;current=target;write(current);}
}
