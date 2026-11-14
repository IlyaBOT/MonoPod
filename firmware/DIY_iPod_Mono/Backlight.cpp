#include "Backlight.h"
#include "BacklightMath.h"
#include "Config.h"
BacklightController backlight;

bool BacklightController::attachPwm() {
  if(pwmAttached) return true;
  if(Config::PIN_TFT_BL < 0) return false;
  pwmAttached=ledcAttach((uint8_t)Config::PIN_TFT_BL,
                         Config::TFT_BL_PWM_HZ,Config::TFT_BL_PWM_BITS);
  if(!pwmAttached) MONOPOD_LOG.println("Backlight: LEDC attach failed");
  return pwmAttached;
}

void BacklightController::driveSteady(bool on) {
  if(Config::PIN_TFT_BL < 0)return;
  const uint8_t pin=(uint8_t)Config::PIN_TFT_BL;
  if(pwmAttached){ledcDetach(pin);pwmAttached=false;}
  pinMode(pin,OUTPUT);
  const bool driveHigh=(on != Config::TFT_BL_ACTIVE_LOW);
  digitalWrite(pin,driveHigh?HIGH:LOW);
}

void BacklightController::begin(uint8_t pct){
  configured=constrain((int)pct,5,100);
  desiredOn=false;current=from=target=0;moving=false;
  ready=Config::PIN_TFT_BL>=0;
  if(!ready)return;
  // Never enable the LED before the UI explicitly requests wake().
  driveSteady(false);
  if(Config::TFT_BL_BRIGHTNESS_CONTROL){
    ready=attachPwm();
    if(ready)write(0);
  }
}

void BacklightController::write(uint8_t percent){
  if(!ready)return;
  // Fixed-brightness mode: no PWM at either resting endpoint.
  // A fade can still use LEDC temporarily for smooth transitions.
  if(!Config::TFT_BL_BRIGHTNESS_CONTROL && (percent==0 || percent>=100)){
    driveSteady(percent>=100);
    return;
  }
  if(!attachPwm())return;
  const uint16_t duty=BacklightMath::gammaDuty(percent);
  const uint16_t maxDuty=(1U<<Config::TFT_BL_PWM_BITS)-1;
  ledcWrite((uint8_t)Config::PIN_TFT_BL,
            Config::TFT_BL_ACTIVE_LOW?maxDuty-duty:duty);
}

void BacklightController::request(bool on){
  const uint8_t next=on?(Config::TFT_BL_BRIGHTNESS_CONTROL?configured:100):0;
  if(desiredOn==on && target==next)return;
  desiredOn=on;from=current;target=next;
  began=millis();lastStep=0;
  fadeMs=on?Config::TFT_BL_FADE_ON_MS:Config::TFT_BL_FADE_OFF_MS;
  moving=(from!=target);
  if(!moving)write(target);
}

void BacklightController::setBrightness(uint8_t pct){
  configured=constrain((int)pct,5,100);
  if(Config::TFT_BL_BRIGHTNESS_CONTROL && desiredOn)request(true);
}
void BacklightController::wake(){request(true);}
void BacklightController::sleep(){request(false);}
void BacklightController::loop(){
  if(!moving)return;
  const uint32_t now=millis();
  const uint32_t elapsed=now-began;
  if(elapsed<fadeMs && now-lastStep<10)return;
  lastStep=now;
  const uint16_t eased=BacklightMath::ease(elapsed,fadeMs);
  const uint8_t value=BacklightMath::mix(from,target,eased);
  if(current!=value){current=value;write(current);}
  if(elapsed>=fadeMs){moving=false;current=target;write(current);}
}
