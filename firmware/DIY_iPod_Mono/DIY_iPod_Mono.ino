/* MonoPod: community implementation inspired by the 2004 monochrome iPod UI.
 * Not Apple's firmware; no Apple ROM, trademarks, artwork or iAP implementation.
 * Requires Arduino ESP32 core v3.x, Adafruit GFX, Adafruit ST7735,
 * ESP8266Audio. Optionally ESP32-A2DP for FEATURE_BLUETOOTH=1.
 */
#include "Config.h"
#include "Settings.h"
#include "Library.h"
#include "Input.h"
#include "Display.h"
#include "AudioEngine.h"
#include "UI.h"
#include "WiFiSync.h"
#include "ClockService.h"
#include "Iap1.h"
#include "Feedback.h"
#include <SPI.h>
void setup(){
  Serial.begin(115200);delay(200);
  Serial.println("\nMonoPod v0.6 / ESP32-WROVER");
  settings.begin();
  SPI.begin(Config::PIN_SPI_SCK,Config::PIN_SPI_MISO,Config::PIN_SPI_MOSI);
  // Each SPI peripheral MUST have its own CS; both high when idle.
  pinMode(Config::PIN_TFT_CS,OUTPUT);digitalWrite(Config::PIN_TFT_CS,HIGH);
  pinMode(Config::PIN_SD_CS,OUTPUT);digitalWrite(Config::PIN_SD_CS,HIGH);
  display.begin();
  if(!media.begin())Serial.println("SD: "+media.lastError());
  else {Serial.printf("SD: %u tracks\n",(unsigned)media.size());
    display.reloadTheme(); // load optional bitmap theme from microSD
  }
  input.begin();
  feedback.begin();
#if FEATURE_AUDIO
  if(!player.begin())Serial.println("Audio: init failed");
#endif
  clockService.begin();
  iap1.begin();
  randomSeed(esp_random());
  ui.begin();
  Serial.println("Ready. Use resistor-ladder keys or touch wheel.");
}
void loop(){
  player.loop(); // prioritize continuous decode / filling I2S DMA buffers
#if FEATURE_WIFI_SYNC
  wifiSync.loop();
#endif
  Key k=input.poll();
  if(k!=Key::None)ui.event(k);
  Key remote=iap1.poll();
  if(remote!=Key::None)ui.event(remote);
  ui.loop();
  feedback.loop();
  settings.loop();
  delay(1);
}
