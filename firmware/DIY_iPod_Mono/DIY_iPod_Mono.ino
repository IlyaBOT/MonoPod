/* MonoPod: community implementation inspired by the 2004 monochrome iPod UI.
 * Not Apple's firmware; no Apple ROM, trademarks, artwork or iAP implementation.
 * Requires Arduino ESP32 core v3.x, Adafruit GFX, Adafruit ST7735,
 * ESP8266Audio. Optionally ESP32-A2DP for FEATURE_BLUETOOTH=1.
 */
#include "Config.h"
#include "Settings.h"
#include "Library.h"
#include "Input.h"
#include "DevConsole.h"
#include "Display.h"
#include "Backlight.h"
#include "AudioEngine.h"
#if !EXCLUDE_BAD_APPLE_EASTEREGG
#include "BadApple.h"
#endif
#include "UI.h"
#include "WiFiSync.h"
#include "WiFiNetworks.h"
#include "ClockService.h"
#include "UsbSync.h"
#include "SyncMode.h"
#include "SyncFiles.h"
#include "IpodIdentity.h"
#include "Feedback.h"
#include "Battery.h"
#if FEATURE_BLUETOOTH
#include "BluetoothOutput.h"
#endif
#include <SPI.h>
#if FEATURE_AUDIO && MONOPOD_BOARD == MONOPOD_BOARD_ESP32_S3
static TaskHandle_t monopodAudioTask=nullptr;
static void audioCoreWorker(void*){
  for(;;){
    player.loop();
    // At least one tick for Wi-Fi/system scheduling. DMA covers this gap.
    vTaskDelay(pdMS_TO_TICKS(1) ? pdMS_TO_TICKS(1) : 1);
  }
}
#endif
void setup(){
  MONOPOD_LOG.begin(115200);
#if FEATURE_USB_SYNC
  Serial.begin(115200); // native USB CDC reserved for MPOD1 v4
#endif
  delay(200);
  MONOPOD_LOG.printf("\nMonoPod v0.10.6.8 / %s\n", MONOPOD_BOARD==MONOPOD_BOARD_ESP32_S3?"ESP32-S3":"original ESP32");
#if defined(ARDUINO_ARCH_ESP32)
  MONOPOD_LOG.printf("Chip=%s rev=%u, flash=%u B, PSRAM=%u B (%s), heap=%u B\n",
    ESP.getChipModel(),(unsigned)ESP.getChipRevision(),
    (unsigned)ESP.getFlashChipSize(),(unsigned)ESP.getPsramSize(),
    psramFound()?"OK":"missing",(unsigned)ESP.getFreeHeap());
  MONOPOD_LOG.printf("Board profile=%s | Bluetooth Classic=%s\n",
    MONOPOD_BOARD==MONOPOD_BOARD_ESP32_S3?"ESP32-S3 N16R8":"ESP32-WROVER",FEATURE_BLUETOOTH?"enabled":"compiled out");
#endif
  MONOPOD_LOG.println("Build: wheel interpolation / adaptive detents / precise seek / reliable clicks");
  settings.begin();
  backlight.begin(settings.data.backlightBrightness);
  battery.begin();
  SPI.begin(Config::PIN_SPI_SCK,Config::PIN_SPI_MISO,Config::PIN_SPI_MOSI);
  // Each SPI peripheral MUST have its own CS; both high when idle.
  pinMode(Config::PIN_TFT_CS,OUTPUT);digitalWrite(Config::PIN_TFT_CS,HIGH);
  pinMode(Config::PIN_SD_CS,OUTPUT);digitalWrite(Config::PIN_SD_CS,HIGH);
  MONOPOD_LOG.println("Boot: SD mount (without media scan)");
  // SD enters SPI mode before the first TFT transaction; keep this order.
  const bool sdMounted=media.begin(false);
  MONOPOD_LOG.println("Boot: TFT and splash");
  display.begin();
  if(!sdMounted)MONOPOD_LOG.println("SD: "+media.lastError());
  else display.reloadTheme(); // theme and custom bootlogo need an already mounted card
  display.bootLogo(); // built-in square or /MonoPod/boot.rgb565
  const uint32_t bootLogoShownAt=millis();
  backlight.wake();
  // Start Wi-Fi immediately: lwIP connects concurrently with the SD scan.
  // SNTP remains asynchronous and is started as soon as a STA IP is available.
  clockService.begin();
  wifiNetworks.begin();
  clockService.loop();
  // Let the short LEDC fade finish before the main-thread SD scan blocks loop().
  // Wi-Fi STA is already joining asynchronously while this runs.
  const uint32_t fadeStarted=millis();
  while((uint32_t)(millis()-fadeStarted)<Config::TFT_BL_FADE_ON_MS+10u){
    backlight.loop();
    wifiNetworks.tick();clockService.loop();
    delay(10);
  }
  MONOPOD_LOG.println("Boot: library scan behind visible splash / STA join asynchronous");
  if(sdMounted){
    const bool recoveryNeeded=SD.exists("/.monopod-transfer.target") ||
      SD.exists("/Music/.monopod-target.txt") || SD.exists("/Music/.monopod-wifi.target");
    SyncFiles::recover();
    media.rescan(); // scan AFTER recovery, behind the splash (not twice)
    MONOPOD_LOG.printf("SD: %u tracks\n",(unsigned)media.size());
    if(recoveryNeeded)MONOPOD_LOG.println("Boot: recovered interrupted SD transfer");
  }
  wifiNetworks.tick();
  clockService.loop();
#if defined(ARDUINO_ARCH_ESP32)
  MONOPOD_LOG.printf("Boot: index complete; loop stack low-water=%u B\n",(unsigned)uxTaskGetStackHighWaterMark(nullptr));
#endif
  // Extra 1s compared with the former 850ms delay; scanning is included.
  while((uint32_t)(millis()-bootLogoShownAt)<Config::BOOT_LOGO_MIN_MS){
    backlight.loop();
    wifiNetworks.tick();clockService.loop();
    delay(10);
  }
  input.begin();
  feedback.begin();
#if FEATURE_AUDIO
  MONOPOD_LOG.println("Boot: audio");
  if(!player.begin())MONOPOD_LOG.println("Audio: init failed");
#endif
  IpodIdentity::begin();
  MONOPOD_LOG.printf("MonoPod device GUID=%s (%s)\n",IpodIdentity::guid(),IpodIdentity::fromEfuse()?"eFuse MAC":"NVS fallback");
  usbSync.begin();
  randomSeed(esp_random());
  MONOPOD_LOG.println("Boot: Wi-Fi and UI");
  // Wi-Fi was already started under the boot logo; no second connection attempt.
  wifiNetworks.tick();clockService.loop();
  ui.begin();
  if(!media.sdOK())ui.notify("No microSD card");
  devConsole.begin();
#if FEATURE_AUDIO && MONOPOD_BOARD == MONOPOD_BOARD_ESP32_S3
  const BaseType_t audioCreated=xTaskCreatePinnedToCore(audioCoreWorker,"MonoPodAudio",
       16384,nullptr,3,&monopodAudioTask,0);
  MONOPOD_LOG.printf("Audio: core 0 dedicated worker %s\n",audioCreated==pdPASS?"OK":"FAILED (loop fallback)");
#endif
  MONOPOD_LOG.println("Ready. Use resistor-ladder keys or touch wheel.");
}
void loop(){
  if(!syncMode.active()
#if !EXCLUDE_BAD_APPLE_EASTEREGG
     && !badApple.active()
#endif
  ) {
#if FEATURE_AUDIO && MONOPOD_BOARD == MONOPOD_BOARD_ESP32_S3
    if(!monopodAudioTask)player.loop(); // low-memory fallback
#else
    player.loop();
#endif
  } // No second task owns Bad Apple's soundtrack
#if FEATURE_WIFI_SYNC
  wifiSync.loop();
#endif
  devConsole.loop();
  // Runtime override: skip ALL hardware polling, including floating HOLD/ADC/touch.
  // No compile-time define or reboot required.
  if(!devConsole.hardwareDisabled()){
    Key k=input.poll();
    if(k!=Key::None)ui.event(k,input.lastEventFromWheel());
  }
  Key virtualKey=devConsole.pollKey();
  if(virtualKey!=Key::None)ui.event(virtualKey);
#if FEATURE_BLUETOOTH
  // Remote keys are forwarded from the Bluetooth callback through a nonblocking queue.
  Key btRemote=BluetoothAudioOutput::pollRemote();
  if(btRemote!=Key::None)ui.event(btRemote);
#endif
  usbSync.loop();
  battery.loop();
  ui.loop();
  clockService.loop();
  feedback.loop();
  settings.loop();
  delay(1);
}
