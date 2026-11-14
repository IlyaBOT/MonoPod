#pragma once
// Hardware target: ORIGINAL ESP32 (ESP32-WROVER, not ESP32-S3/C3).
// Display: ST7735S 1.8", 128x160 pixels, rotated to 160x128.
// Arduino-ESP32 3.x; ESP8266Audio + Adafruit GFX/ST7735.
#include <Arduino.h>
// Compile-time hardware selection. The user-facing menu selects the click ROUTE.
// NONE: no local clicker; PIEZO: GPIO2; HAPTIC: DRV2605L via I2C.
#define CLICKER_HW_NONE   0
#define CLICKER_HW_PIEZO  1
#define CLICKER_HW_HAPTIC 2
#ifndef CLICKER_HARDWARE
#define CLICKER_HARDWARE CLICKER_HW_PIEZO
#endif
#if CLICKER_HARDWARE < CLICKER_HW_NONE || CLICKER_HARDWARE > CLICKER_HW_HAPTIC
#error "Unsupported CLICKER_HARDWARE value"
#endif
#define FEATURE_PIEZO (CLICKER_HARDWARE == CLICKER_HW_PIEZO)
#define FEATURE_HAPTIC_DRV2605 (CLICKER_HARDWARE == CLICKER_HW_HAPTIC)

// WOKWI_SIMULATION=1: ILI9341/individual GPIO keys, no touch, no I2S.
// Arduino IDE physical hardware is the default (ST7735/ADC ladder).
#ifndef WOKWI_SIMULATION
#define WOKWI_SIMULATION 0
#endif
#ifndef FEATURE_AUDIO
#define FEATURE_AUDIO (!WOKWI_SIMULATION)
#endif

namespace Config {
constexpr uint8_t PIN_SPI_SCK  = 18;
constexpr uint8_t PIN_SPI_MISO = 19;
constexpr uint8_t PIN_SPI_MOSI = 23;
constexpr uint8_t PIN_TFT_CS   = 5;   // BOOT strap: display must NOT pull this low on reset
constexpr uint8_t PIN_TFT_DC   = 22;
constexpr int8_t  PIN_TFT_RST  = -1;  // Wire TFT RESET to ESP32 EN, or use RC reset
constexpr uint8_t PIN_SD_CS    = 21;
constexpr uint8_t PIN_I2S_BCLK = 26;
constexpr uint8_t PIN_I2S_LRC  = 25;
constexpr uint8_t PIN_I2S_DATA = 15; // BOOT strap; DAC input must be high impedance at boot
constexpr uint8_t PIN_BUTTON_ADC = 34; // ADC1, external 10k pull-up to 3V3
constexpr uint8_t PIN_HOLD = 35;       // external 10k pull-up, switch to ground
// Six sectors in the default Piezo prototype.  With DRV2605 enabled,
// GPIO32/33 become I2C, so a FOUR-sector wheel must be wired instead.
#if FEATURE_HAPTIC_DRV2605
constexpr uint8_t WHEEL_PINS[4] = {4,13,14,27};
constexpr uint8_t WHEEL_COUNT = 4;
#else
constexpr uint8_t WHEEL_PINS[6] = {4,13,14,27,33,32};
constexpr uint8_t WHEEL_COUNT = 6;
#endif
// GPIO2 is a strapping pin; use ONLY a passive high-impedance piezo.
constexpr uint8_t PIN_PIEZO = 2; // strap pin: ONLY passive high-Z piezo; no external pullup
// Simulation keys use independent buttons (active-low, INPUT_PULLUP).
constexpr uint8_t PIN_KEY_MENU = 4;
constexpr uint8_t PIN_KEY_PREVIOUS = 13;
constexpr uint8_t PIN_KEY_NEXT = 14;
constexpr uint8_t PIN_KEY_PLAY = 27;
constexpr uint8_t PIN_KEY_SELECT = 32;
constexpr uint8_t PIN_HAPTIC_SDA = 32;
constexpr uint8_t PIN_HAPTIC_SCL = 33;
constexpr uint16_t PIEZO_SCROLL_HZ = 3400;
constexpr uint16_t PIEZO_ACTION_HZ = 2700;
constexpr uint16_t FEEDBACK_SCROLL_MIN_MS = 45;
constexpr uint16_t FEEDBACK_ACTION_MIN_MS = 60;
// Optional mechanical encoder for development, external 10k pull-ups:
constexpr uint8_t ENCODER_A = 36, ENCODER_B = 39;
constexpr uint8_t PIN_IAP1_RX = 39; // optional 3.3V UART, RX-only Mode 2 prototype
constexpr uint32_t SPI_SD_HZ = 10000000;
constexpr uint8_t TFT_ROTATION = 1;
constexpr uint8_t MAX_VOLUME = 90; // 90/100 maximum by design, initial is 30
constexpr size_t MAX_TRACKS = 350;
constexpr uint8_t MAX_SCAN_DEPTH = 6;
constexpr char WIFI_AP_SSID[] = "MonoPod-Sync";
constexpr char WIFI_AP_PASS[] = "MonoPod2026!"; // CHANGE before enabling Wi-Fi Sync
constexpr char BT_TARGET_NAME[] = ""; // exact discovered headphone name (if BT enabled)
constexpr uint16_t SCREEN_W = 160, SCREEN_H = 128;
constexpr char TIMEZONE_TZ[] = "UTC0"; // POSIX TZ string for user timezone
}
#ifndef FEATURE_TOUCH_WHEEL
#define FEATURE_TOUCH_WHEEL (!WOKWI_SIMULATION)
#endif
#ifndef FEATURE_DEV_ENCODER
#define FEATURE_DEV_ENCODER 0
#endif
#ifndef FEATURE_WIFI_SYNC
#define FEATURE_WIFI_SYNC (!WOKWI_SIMULATION)
#endif
#ifndef FEATURE_BLUETOOTH
#define FEATURE_BLUETOOTH 0 // Needs ESP32-A2DP; experimental 44.1 kHz path
#endif
#ifndef FEATURE_FLAC
#define FEATURE_FLAC 1
#endif
#ifndef FEATURE_AAC
#define FEATURE_AAC 1
#endif

#ifndef FEATURE_IAP1_RX
#define FEATURE_IAP1_RX 0  // Mode 2 receive-only iAP1, EXPERIMENTAL
#endif
#if FEATURE_IAP1_RX && FEATURE_DEV_ENCODER
#error "iAP1 UART RX on GPIO39 conflicts with developer encoder B"
#endif
