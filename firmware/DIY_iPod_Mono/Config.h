#pragma once
// Board profiles: ESP32 original or ESP32-S3 DevKitC N16R8.
// Display: ST7735S 1.8", 128x160 pixels, rotated to 160x128.
// Arduino-ESP32 3.x; ESP8266Audio + Adafruit GFX/ST7735.
#include <Arduino.h>
// Select the board at COMPILE time, not in the runtime Settings screen.
// AUTO follows the board selected in Arduino IDE; explicit selection is possible.
#define FEATURE_SD_FORMAT 1
#define MONOPOD_BOARD_ESP32 1
#define MONOPOD_BOARD_ESP32_S3 2
#ifndef MONOPOD_BOARD
  #if defined(CONFIG_IDF_TARGET_ESP32S3)
    #define MONOPOD_BOARD MONOPOD_BOARD_ESP32_S3
  #else
    #define MONOPOD_BOARD MONOPOD_BOARD_ESP32
  #endif
#endif
#if MONOPOD_BOARD != MONOPOD_BOARD_ESP32 && MONOPOD_BOARD != MONOPOD_BOARD_ESP32_S3
  #error "Select MONOPOD_BOARD_ESP32 or MONOPOD_BOARD_ESP32_S3"
#endif
// The selected Arduino core MUST match the selected hardware profile.
#if defined(CONFIG_IDF_TARGET_ESP32S3) && MONOPOD_BOARD != MONOPOD_BOARD_ESP32_S3
  #error "Arduino IDE is targeting ESP32-S3 but Config.h selects classic ESP32"
#endif
#if defined(CONFIG_IDF_TARGET_ESP32) && MONOPOD_BOARD != MONOPOD_BOARD_ESP32
  #error "Arduino IDE is targeting classic ESP32 but Config.h selects ESP32-S3"
#endif
#if defined(ARDUINO_ARCH_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32S3)
  #error "MonoPod supports ESP32 and ESP32-S3 only (not C3/C6/S2 etc.)"
#endif
#if MONOPOD_BOARD == MONOPOD_BOARD_ESP32_S3
  #define MONOPOD_HAS_BT_CLASSIC 0
#else
  #define MONOPOD_HAS_BT_CLASSIC 1
#endif
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
// ESP32-S3 DevKitC-1 / N16R8: no use of USB19/20, PSRAM35-37,
// UART43/44, strapping GPIO0/45/46 or RGB LED GPIO38/48.
// These S3 pins are a NEW wiring plan; never reuse the WROVER cable harness.
#if MONOPOD_BOARD == MONOPOD_BOARD_ESP32_S3
constexpr uint8_t PIN_SPI_SCK = 12;
constexpr uint8_t PIN_SPI_MISO = 13;
constexpr uint8_t PIN_SPI_MOSI = 11;
constexpr uint8_t PIN_TFT_CS = 10;
// TFT backlight PWM, GPIO15 free on the S3 prototype; not the onboard RGB LED.
constexpr int8_t PIN_TFT_BL = 15;
constexpr uint8_t PIN_TFT_DC = 39;
constexpr int8_t PIN_TFT_RST = 47; // dedicated reset, actual physical wire
constexpr uint8_t PIN_SD_CS = 14;
constexpr uint8_t PIN_I2S_BCLK = 16;
constexpr uint8_t PIN_I2S_LRC = 17;
constexpr uint8_t PIN_I2S_DATA = 18;
constexpr uint8_t PIN_BUTTON_ADC = 9; // ADC1, 10k pull-up / resistor ladder
constexpr uint8_t PIN_BATTERY_ADC = 4; // ADC1, 100k/100k divider
constexpr uint8_t PIN_HOLD = 21; // external 10k pull-up
constexpr uint8_t WHEEL_PINS[6] = {1,2,5,6,7,8}; // all actual S3 touch channels
constexpr uint8_t WHEEL_COUNT = 6;
constexpr uint8_t PIN_PIEZO = 42;
// GPIO40/41 are also JTAG pins; only use with no external JTAG.
constexpr uint8_t PIN_HAPTIC_SDA = 40;
constexpr uint8_t PIN_HAPTIC_SCL = 41;
constexpr uint8_t PIN_KEY_MENU = 4; // Wokwi emulates independent keys
constexpr uint8_t PIN_KEY_PREVIOUS = 13;
constexpr uint8_t PIN_KEY_NEXT = 14;
constexpr uint8_t PIN_KEY_PLAY = 27;
constexpr uint8_t PIN_KEY_SELECT = 32;
constexpr uint8_t ENCODER_A = 15, ENCODER_B = 40; // disabled in Rev A (developer-only)
#else
// Original ESP32-WROVER prototype wiring (unchanged).
constexpr uint8_t PIN_SPI_SCK  = 18;
constexpr uint8_t PIN_SPI_MISO = 19;
constexpr uint8_t PIN_SPI_MOSI = 23;
constexpr uint8_t PIN_TFT_CS   = 5;
// No confirmed free safe GPIO on the WROVER harness; leave BL disabled.
constexpr int8_t PIN_TFT_BL = -1;
constexpr uint8_t PIN_TFT_DC   = 22;
// ST7735 RST connects to EN/reset on WROVER; -1 means no dedicated GPIO.
constexpr int8_t PIN_TFT_RST = -1;
constexpr uint8_t PIN_SD_CS   = 21;
constexpr uint8_t PIN_I2S_BCLK = 26;
constexpr uint8_t PIN_I2S_LRC = 25;
constexpr uint8_t PIN_I2S_DATA = 15;
constexpr uint8_t PIN_BUTTON_ADC = 34;
constexpr uint8_t PIN_BATTERY_ADC = 36;
constexpr uint8_t PIN_HOLD = 35;
#if FEATURE_HAPTIC_DRV2605
constexpr uint8_t WHEEL_PINS[4] = {4,13,14,27};
constexpr uint8_t WHEEL_COUNT = 4;
#else
constexpr uint8_t WHEEL_PINS[6] = {4,13,14,27,33,32};
constexpr uint8_t WHEEL_COUNT = 6;
#endif
constexpr uint8_t PIN_PIEZO = 2;
constexpr uint8_t PIN_KEY_MENU = 4;
constexpr uint8_t PIN_KEY_PREVIOUS = 13;
constexpr uint8_t PIN_KEY_NEXT = 14;
constexpr uint8_t PIN_KEY_PLAY = 27;
constexpr uint8_t PIN_KEY_SELECT = 32;
constexpr uint8_t PIN_HAPTIC_SDA = 32;
constexpr uint8_t PIN_HAPTIC_SCL = 33;
constexpr uint8_t ENCODER_A = 36, ENCODER_B = 39;
#endif
constexpr uint16_t PIEZO_SCROLL_HZ = 3400;
constexpr uint16_t PIEZO_ACTION_HZ = 2700;
constexpr uint16_t FEEDBACK_SCROLL_MIN_MS = 45;
constexpr uint16_t FEEDBACK_ACTION_MIN_MS = 60;
constexpr uint32_t SPI_SD_HZ = 10000000;
constexpr uint8_t TFT_ROTATION = 1;
// P-channel high-side MOSFET on the separate TFT LED/BL anode: LOW = ON.
// Set false only when using verified active-high PWM hardware.
constexpr bool TFT_BL_ACTIVE_LOW = true;
constexpr uint32_t TFT_BL_PWM_HZ = 16000;
constexpr uint8_t TFT_BL_PWM_BITS = 10;
constexpr uint16_t TFT_BL_FADE_ON_MS = 250;
constexpr uint16_t TFT_BL_FADE_OFF_MS = 400;
constexpr uint8_t MAX_VOLUME = 90; // 90/100 maximum by design, initial is 30
// Index capacity, NOT a limit on microSD file count. Target: >=1024 tracks.
// 2048 is the configurable scan ceiling; PSRAM is recommended for real use.
constexpr size_t MAX_TRACKS = 2048;
constexpr uint8_t MAX_SCAN_DEPTH = 6;
constexpr char WIFI_AP_SSID[] = "MonoPod-Sync";
constexpr char WIFI_AP_PASS[] = "MonoPod2026!"; // CHANGE before enabling Wi-Fi Sync
#if MONOPOD_HAS_BT_CLASSIC
constexpr char BT_TARGET_NAME[] = ""; // only built for original ESP32
#endif
constexpr uint16_t SCREEN_W = 160, SCREEN_H = 128;
constexpr char TIMEZONE_TZ[] = "UTC0"; // POSIX TZ string for user timezone
}
// Battery voltage through a 100k/100k divider at PIN_BATTERY_ADC (ADC1).
// Wokwi has no physical battery: it shows a documented demo charge level.
#ifndef FEATURE_BATTERY_ADC
#define FEATURE_BATTERY_ADC (!WOKWI_SIMULATION)
#endif
#ifndef WOKWI_BATTERY_DEMO_PERCENT
#define WOKWI_BATTERY_DEMO_PERCENT 75
#endif
#ifndef FEATURE_TOUCH_WHEEL
#define FEATURE_TOUCH_WHEEL (!WOKWI_SIMULATION)
#endif
#ifndef FEATURE_DEV_ENCODER
#define FEATURE_DEV_ENCODER 0
#endif
#if FEATURE_BATTERY_ADC && FEATURE_DEV_ENCODER && MONOPOD_BOARD == MONOPOD_BOARD_ESP32
#error "Battery ADC GPIO36 conflicts with developer encoder GPIO36 on classic ESP32"
#endif
#if FEATURE_DEV_ENCODER && MONOPOD_BOARD == MONOPOD_BOARD_ESP32_S3
#error "S3 GPIO15 backlight PWM conflicts with developer encoder GPIO15; disable unused encoder"
#endif
#if FEATURE_DEV_ENCODER && FEATURE_HAPTIC_DRV2605 && MONOPOD_BOARD == MONOPOD_BOARD_ESP32_S3
#error "ESP32-S3 developer encoder GPIO40 conflicts with DRV2605 I2C SDA"
#endif
#ifndef FEATURE_WIFI_SYNC
#define FEATURE_WIFI_SYNC (!WOKWI_SIMULATION)
#endif
// Bluetooth Classic A2DP source/AVRCP is only present on original ESP32.
// On S3, force the feature out at preprocessing time: no includes, class,
// buffers, strings, UI pages or linking against the ESP32-A2DP library.
#if MONOPOD_BOARD == MONOPOD_BOARD_ESP32_S3
  #if defined(FEATURE_BLUETOOTH) && FEATURE_BLUETOOTH
    #error "ESP32-S3 cannot use Bluetooth Classic/A2DP. Set FEATURE_BLUETOOTH=0."
  #endif
  #undef FEATURE_BLUETOOTH
  #define FEATURE_BLUETOOTH 0
#else
  #ifndef FEATURE_BLUETOOTH
    #define FEATURE_BLUETOOTH 0 // opt in on ORIGINAL ESP32 only (experimental)
  #endif
#endif
#ifndef FEATURE_FLAC
#define FEATURE_FLAC 1
#endif
#ifndef FEATURE_AAC
#define FEATURE_AAC 1
#endif

#ifndef FEATURE_SD_FORMAT
#define FEATURE_SD_FORMAT 0 // opt-in! requires SdFat v2, test with expendable card first
#endif


// MonoPod USB Sync v1: native ESP32-S3 USB CDC (GPIO19 D-, GPIO20 D+).
// USB sync is NOT Apple's iAP and does not implement iTunes pairing.
#ifndef FEATURE_USB_SYNC
#define FEATURE_USB_SYNC (MONOPOD_BOARD == MONOPOD_BOARD_ESP32_S3 && !WOKWI_SIMULATION)
#endif
#if FEATURE_USB_SYNC && MONOPOD_BOARD != MONOPOD_BOARD_ESP32_S3
#error "Native USB Sync currently requires ESP32-S3"
#endif
#if FEATURE_USB_SYNC && (!defined(ARDUINO_USB_CDC_ON_BOOT) || !ARDUINO_USB_CDC_ON_BOOT)
#error "Enable Tools > USB CDC On Boot in Arduino IDE for MonoPod USB Sync"
#endif
// Protect the USB transfer channel from firmware log lines.
#if FEATURE_USB_SYNC
#define MONOPOD_LOG Serial0
#else
#define MONOPOD_LOG Serial
#endif
