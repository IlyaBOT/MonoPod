#include "PowerGuard.h"

#if MONOPOD_BOARD == MONOPOD_BOARD_ESP32_S3
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include "AudioEngine.h"
#include "Backlight.h"
#include "Settings.h"
#include "SyncMode.h"
#include "WiFiSync.h"
#include "UsbSync.h"

namespace {
constexpr uint32_t kStateMagic = 0x4D504748; // 'MPGH'
constexpr uint8_t kWaitLock = 1;
constexpr uint8_t kWaitUnlock = 2;
// GPIO9 is wired to 3V3 through external 20k; the ADC saturates at the top.
// Only a *released* reading is a usable rail estimate. At normal supply the
// released reading was 3148mV; SELECT never exceeded 2217mV in field tests.
constexpr uint16_t kReleasedMinMv = 2450;
constexpr uint16_t kShutdownMv = 3050;  // don't deliberately run below 3.0V S3 spec
constexpr uint16_t kBootResumeMv = 3120; // hysteresis; approximate ADC reading
constexpr uint32_t kSustainedLowMs = 750;
constexpr uint32_t kProbeEveryMs = 180;
RTC_DATA_ATTR uint32_t rtcMagic = 0;
RTC_DATA_ATTR uint8_t rtcStage = 0;
uint32_t belowSince = 0;
uint32_t lastProbe = 0;

void configureWakeInput() {
  // EXT0 wakes in RTC IO mode: restore normal digital GPIO before reading.
  rtc_gpio_deinit((gpio_num_t)Config::PIN_HOLD);
  pinMode(Config::PIN_HOLD, INPUT); // external 10k pull-up; LOCK -> GND
}

bool holdLocked() {return digitalRead(Config::PIN_HOLD)==LOW;}

bool stableHoldLocked() {
  const bool value = holdLocked();
  delay(55);
  return value == holdLocked() ? value : holdLocked();
}

uint16_t measureReleasedRail() {
  // With no button pressed, GPIO9 is directly pulled up to the same 3V3 rail.
  // At >=~3.1V the ADC may saturate; low-voltage region is what matters here.
  pinMode(Config::PIN_BUTTON_ADC, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(Config::PIN_BUTTON_ADC, ADC_11db);
  (void)analogReadMilliVolts(Config::PIN_BUTTON_ADC);
  uint16_t values[7];
  for (int i=0; i<7; ++i) {
    values[i] = analogReadMilliVolts(Config::PIN_BUTTON_ADC);
    delayMicroseconds(150);
  }
  // Median: survive a single ADC glitch.
  for (int i=1; i<7; ++i) {
    uint16_t value=values[i];
    int j=i;
    while (j>0 && values[j-1]>value) { values[j]=values[j-1];--j; }
    values[j]=value;
  }
  return values[3];
}

bool credibleReleased(uint16_t mv) {
  // 0..~2.3 V can be any pressed resistor-ladder key. Never shut down
  // on such a sample, nor claim we know the supply voltage in that state.
  return mv>=kReleasedMinMv && mv<=3300;
}

void warningScreen() {
  // Intentionally no SD, settings, Wi-Fi, touch, PSRAM framebuffer or audio.
  SPI.begin(Config::PIN_SPI_SCK,Config::PIN_SPI_MISO,Config::PIN_SPI_MOSI);
  pinMode(Config::PIN_SD_CS,OUTPUT); digitalWrite(Config::PIN_SD_CS,HIGH);
  pinMode(Config::PIN_TFT_CS,OUTPUT);digitalWrite(Config::PIN_TFT_CS,HIGH);
  Adafruit_ST7735 tft(Config::PIN_TFT_CS,Config::PIN_TFT_DC,Config::PIN_TFT_RST);
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(Config::TFT_ROTATION);
  tft.setSPISpeed(Config::SPI_TFT_HZ);
  tft.fillScreen(ST77XX_WHITE);
  // Battery outline, nub and small empty-energy marker.
  tft.drawRect(55,18,48,24,ST77XX_BLACK);
  tft.fillRect(103,25,4,10,ST77XX_BLACK);
  tft.fillRect(59,22,6,16,ST77XX_BLACK);
  tft.setTextWrap(false);
  tft.setTextColor(ST77XX_BLACK,ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(46,61);tft.print("LOW BATTERY");
  tft.setCursor(34,78);tft.print("Connect charger");
  if(Config::PIN_TFT_BL>=0) {
    pinMode((uint8_t)Config::PIN_TFT_BL,OUTPUT);
    digitalWrite((uint8_t)Config::PIN_TFT_BL,Config::TFT_BL_ACTIVE_LOW?LOW:HIGH);
  }
  delay(3000);
  if(Config::PIN_TFT_BL>=0)
    digitalWrite((uint8_t)Config::PIN_TFT_BL,Config::TFT_BL_ACTIVE_LOW?HIGH:LOW);
  tft.fillScreen(ST77XX_BLACK);
}

[[noreturn]] void enterSleep(uint8_t stage, bool wakeOnLock) {
  rtcMagic=kStateMagic;
  rtcStage=stage;
  // external HOLD pull-up is essential. EXT0 is level-triggered, not edge.
  // Wait for an OPPOSITE state so sleep does not instantly retrigger.
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  const esp_err_t status=esp_sleep_enable_ext0_wakeup(
    (gpio_num_t)Config::PIN_HOLD, wakeOnLock ? 0 : 1);
  MONOPOD_LOG.printf("[POWER] Deep sleep; waiting HOLD=%s; wake configuration=%d\n",
    wakeOnLock?"LOCK":"UNLOCK",(int)status);
  MONOPOD_LOG.flush();
  // If EXT0 fails, still enter deep sleep (recoverable by EN/reset or USB).
  esp_deep_sleep_start();
  for (;;) delay(1000);
}

[[noreturn]] void sleepFromCurrentHold() {
  configureWakeInput();
  if (stableHoldLocked()) {
    // HOLD is already locked. Wait for unlock as the second half of motion.
    enterSleep(kWaitUnlock,false);
  }
  enterSleep(kWaitLock,true);
}

[[noreturn]] void warnAndSleep(uint16_t mv) {
  MONOPOD_LOG.printf("[POWER] LOW 3V3 PROXY=%u mV. Show charger screen, sleep.\n",(unsigned)mv);
  // Runtime only: safe if audio wasn't initialized during early boot.
  warningScreen();
  sleepFromCurrentHold();
}
} // namespace
#endif

void PowerGuard::earlyBoot() {
#if MONOPOD_BOARD == MONOPOD_BOARD_ESP32_S3
  configureWakeInput();
  const bool ext0Woke=(esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_EXT0);
  if (!ext0Woke || rtcMagic!=kStateMagic) {
    rtcMagic=0;rtcStage=0;
  } else if (rtcStage==kWaitLock) {
    if (stableHoldLocked()) {
      MONOPOD_LOG.println("[POWER] HOLD locked: waiting to unlock");
      enterSleep(kWaitUnlock,false);
    }
    // Bounced switch or reset: safely reset the state on next sleep.
    rtcStage=0;
  } else if (rtcStage==kWaitUnlock) {
    if (stableHoldLocked()) {
      enterSleep(kWaitUnlock,false);
    }
    MONOPOD_LOG.println("[POWER] HOLD cycle finished: checking rail");
    rtcStage=0;
  }
  const uint16_t railMv=measureReleasedRail();
  MONOPOD_LOG.printf("[POWER] Boot GPIO9(open)= %u mV (3V3 proxy; not a rail voltmeter)\n",railMv);
  if (credibleReleased(railMv) && railMv < kBootResumeMv) warnAndSleep(railMv);
  // If a button is held the rail cannot be inferred from GPIO9. In that case
  // continue normal startup; a dedicated divided 3V3 ADC is a future fix.
#endif
}

void PowerGuard::loop() {
#if MONOPOD_BOARD == MONOPOD_BOARD_ESP32_S3
  const uint32_t now=millis();
  if ((uint32_t)(now-lastProbe)<kProbeEveryMs) return;
  lastProbe=now;
  // Avoid interrupting an active sync/OTA write; prevent updates at low
  // supply in a dedicated OTA preflight check in a later hardware revision.
  if(wifiSync.busy() || usbSync.busy()) { belowSince=0;return; }
  const uint16_t railMv=measureReleasedRail();
  if(!credibleReleased(railMv)) {belowSince=0;return;}
  if(railMv>kShutdownMv) {belowSince=0;return;}
  if(!belowSince) belowSince=now;
  if((uint32_t)(now-belowSince)<kSustainedLowMs) return;
  MONOPOD_LOG.printf("[POWER] Sustained low supply proxy: %u mV\n",railMv);
  // No flash writes while undervoltage: never flush NVS here.
  player.stop();
  backlight.sleep();
  warnAndSleep(railMv);
#endif
}

bool PowerGuard::voltageSafeForOta() {
#if MONOPOD_BOARD == MONOPOD_BOARD_ESP32_S3
  const uint16_t mv=measureReleasedRail();
  // Fail closed if a key is held or the released rail is too low to verify.
  return credibleReleased(mv) && mv>=kBootResumeMv;
#else
  return true;
#endif
}
