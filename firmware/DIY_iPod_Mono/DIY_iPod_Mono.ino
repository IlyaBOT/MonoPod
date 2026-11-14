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
#include "UI.h"
#include "WiFiSync.h"
#include "WiFiNetworks.h"
#include "ClockService.h"
#include "UsbSync.h"
#include "SyncMode.h"
#include "IpodIdentity.h"
#include "Feedback.h"
#include "Battery.h"
#if FEATURE_BLUETOOTH
#include "BluetoothOutput.h"
#endif
#include <SPI.h>
void setup(){
  MONOPOD_LOG.begin(115200);
#if FEATURE_USB_SYNC
  Serial.begin(115200); // native USB CDC is reserved for MonoPod USB Sync v1
#endif
  delay(200);
  MONOPOD_LOG.printf("\nMonoPod v0.8.9.2 / %s\n", MONOPOD_BOARD==MONOPOD_BOARD_ESP32_S3?"ESP32-S3":"original ESP32");
#if defined(ARDUINO_ARCH_ESP32) && !WOKWI_SIMULATION
  MONOPOD_LOG.printf("Chip=%s rev=%u, flash=%u B, PSRAM=%u B (%s), heap=%u B\n",
    ESP.getChipModel(),(unsigned)ESP.getChipRevision(),
    (unsigned)ESP.getFlashChipSize(),(unsigned)ESP.getPsramSize(),
    psramFound()?"OK":"missing",(unsigned)ESP.getFreeHeap());
  MONOPOD_LOG.printf("Board profile=%s | Bluetooth Classic=%s\n",
    MONOPOD_BOARD==MONOPOD_BOARD_ESP32_S3?"ESP32-S3 N16R8":"ESP32-WROVER",FEATURE_BLUETOOTH?"enabled":"compiled out");
#endif
  settings.begin();
  backlight.begin(settings.data.backlightBrightness);
  battery.begin();
  SPI.begin(Config::PIN_SPI_SCK,Config::PIN_SPI_MISO,Config::PIN_SPI_MOSI);
  // Each SPI peripheral MUST have its own CS; both high when idle.
  pinMode(Config::PIN_TFT_CS,OUTPUT);digitalWrite(Config::PIN_TFT_CS,HIGH);
  pinMode(Config::PIN_SD_CS,OUTPUT);digitalWrite(Config::PIN_SD_CS,HIGH);
  const bool sdMounted=media.begin(); // IMPORTANT: initialize SD SPI mode BEFORE first TFT transaction
  display.begin();
  if(!sdMounted)MONOPOD_LOG.println("SD: "+media.lastError());
  else {MONOPOD_LOG.printf("SD: %u tracks\n",(unsigned)media.size());
    display.reloadTheme(); // load optional bitmap theme from microSD
  }
  input.begin();
  feedback.begin();
#if FEATURE_AUDIO
  if(!player.begin())MONOPOD_LOG.println("Audio: init failed");
#endif
  clockService.begin();
  IpodIdentity::begin();
  MONOPOD_LOG.printf("MonoPod device GUID=%s (%s)\n",IpodIdentity::guid(),IpodIdentity::fromEfuse()?"eFuse MAC":"NVS fallback");
  usbSync.begin();
  randomSeed(esp_random());
  wifiNetworks.begin(); // restore last successful Wi-Fi connection from NVS at boot
  ui.begin();
  if(!media.sdOK())ui.notify("No microSD card");
  devConsole.begin();
  MONOPOD_LOG.println("Ready. Use resistor-ladder keys or touch wheel.");
}
void loop(){
  if(!syncMode.active())player.loop(); // SD and I2S are idle during sync
#if FEATURE_WIFI_SYNC
  wifiSync.loop();
#endif
  devConsole.loop();
  // Runtime override: skip ALL hardware polling, including floating HOLD/ADC/touch.
  // No compile-time define or reboot required.
  if(!devConsole.hardwareDisabled()){
    Key k=input.poll();
    if(k!=Key::None)ui.event(k);
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
  feedback.loop();
  settings.loop();
  delay(1);
}
