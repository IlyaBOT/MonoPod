#pragma once
// Board profiles: ESP32 original or ESP32-S3 DevKitC N16R8.
// Display: ST7735S 1.8", 128x160 pixels, rotated to 160x128.
// Arduino-ESP32 3.x; ESP8266Audio + Adafruit GFX/ST7735.
#include <Arduino.h>
// Select the board at COMPILE time, not in the runtime Settings screen.
// AUTO follows the board selected in Arduino IDE; explicit selection is possible.
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

#ifndef FEATURE_AUDIO
#define FEATURE_AUDIO 1
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
constexpr uint8_t PIN_BUTTON_ADC = 9; // ADC1, external 20k pull-up to 3V3
// Five normally-open buttons connect GPIO9 to GND through separate resistors:
// MENU=0, PREV=5.1k, NEXT=10k, PLAY=20k, SELECT=39.9k.
// Required: one physical 20k resistor between GPIO9 and 3V3.
constexpr bool BUTTON_ADC_INTERNAL_PULLUP = false;
// Measured GPIO9 levels with external 20k pull-up (mV): MENU=0, PREV=654..657, NEXT=1064..1085, PLAY=1604..1702, SELECT=2194..2217.
// Idle/transient 3148 mV is NOT a PLAY press.
// Windows allow analog tolerances and preserve gaps between keys.
constexpr uint16_t BUTTON_ADC_WINDOWS_MV[5][2] = {
  { 0, 120 }, { 530, 800 }, { 940, 1220 }, { 1480, 1840 }, { 2040, 2410 }
};
constexpr uint8_t PIN_BATTERY_ADC = 4; // ADC1, 100k/100k divider
constexpr uint8_t PIN_HOLD = 21; // external 10k pull-up
constexpr uint8_t WHEEL_PINS[6] = {1,2,5,6,7,8}; // all actual S3 touch channels
constexpr uint8_t WHEEL_COUNT = 6;
constexpr uint8_t PIN_PIEZO = 42;
// GPIO40/41 are also JTAG pins; only use with no external JTAG.
constexpr uint8_t PIN_HAPTIC_SDA = 40;
constexpr uint8_t PIN_HAPTIC_SCL = 41;
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
// Classic ESP32 GPIO34 cannot use an internal pull-up: retain external 10k resistor.
constexpr bool BUTTON_ADC_INTERNAL_PULLUP = false;
constexpr int BUTTON_ADC_BOUNDS_MV[5] = {200, 820, 1290, 1840, 2580};
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
constexpr uint8_t PIN_HAPTIC_SDA = 32;
constexpr uint8_t PIN_HAPTIC_SCL = 33;
#endif
// Debounce / release guard apply to both ESP32 and ESP32-S3.
constexpr uint16_t BUTTON_ADC_DEBOUNCE_MS = 60;
constexpr uint16_t BUTTON_ADC_RELEASE_GUARD_MS = 100;
constexpr uint16_t PIEZO_SCROLL_HZ = 3400;
constexpr uint16_t PIEZO_ACTION_HZ = 2700;
constexpr uint16_t FEEDBACK_SCROLL_MIN_MS = 45;
constexpr uint16_t FEEDBACK_ACTION_MIN_MS = 60;
// Single SD SPI frequency, no diagnostic probing or speed fallback.
// Conservative starting speed for breadboard wiring; tune after real testing.
constexpr uint32_t SPI_SD_HZ = 8000000; // 4 MHz fallback if wiring/card is unreliable
// ST7735S was at 16MHz. 20/26.67MHz are optional OVERCLOCK values,
// not guaranteed by ST7735S datasheet. Keep stable default first.
constexpr uint32_t SPI_TFT_HZ = 20000000; // experimentally test 20000000 later
constexpr uint8_t TFT_ROTATION = 1;
// P-channel high-side MOSFET on the separate TFT LED/BL anode: LOW = ON.
// Set false only when using verified active-high PWM hardware.
constexpr bool TFT_BL_ACTIVE_LOW = false; // direct GPIO15 LED/BL, active-high; set true for P-MOS inverter
// false: force 100% ON as a steady GPIO level, PWM only during fade.
// true: respect the user brightness setting and keep LEDC PWM enabled.
// NOTE: steady HIGH cannot fix an overcurrent/voltage sag caused by directly
// driving the TFT LED/backlight from the ESP32 GPIO. Use a transistor driver.
constexpr bool TFT_BL_BRIGHTNESS_CONTROL = false;
constexpr uint32_t TFT_BL_PWM_HZ = 16000;
constexpr uint8_t TFT_BL_PWM_BITS = 10;
constexpr uint16_t TFT_BL_FADE_ON_MS = 250;
// Calibration from prototype measurement: Vbat=4.23 V when GPIO4=2.10 V.
// Applied to analogReadMilliVolts() output, not raw 12-bit ADC counts.
// Effective conversion: Vbat = ADC_mV * BATTERY_ADC_RATIO / 1000 + BATTERY_ADC_OFFSET_V.
constexpr float BATTERY_ADC_RATIO = 4.23f / 2.10f;
constexpr float BATTERY_ADC_OFFSET_V = 0.0f;
constexpr float BATTERY_FULL_VOLTAGE = 4.23f;
// Voltage-only charge indication (NOT proof of current flowing into the cell).
// Starts STRICTLY above 2.15 V at GPIO4, clears below 2.13 V (20 mV hysteresis).
// Use a TP4056 CHRG/STAT signal for reliable charging detection on future PCB.
constexpr uint16_t BATTERY_CHARGE_ADC_ON_MV = 2150;
constexpr uint16_t BATTERY_CHARGE_ADC_OFF_MV = 2130;
constexpr uint32_t BATTERY_SAMPLE_INTERVAL_MS = 60000UL;
constexpr uint16_t TFT_BL_FADE_OFF_MS = 400;
// Minimum visible splash duration; SD library scan and STA connection overlap it.
constexpr uint32_t BOOT_LOGO_MIN_MS = 1850UL;
constexpr uint8_t MAX_VOLUME = 100; // clamp 0..100, no wraparound
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
// Real prototype ADC, six capacitive wheel electrodes and Wi-Fi Sync.
#ifndef FEATURE_BATTERY_ADC
#define FEATURE_BATTERY_ADC 1
#endif
#ifndef FEATURE_TOUCH_WHEEL
#define FEATURE_TOUCH_WHEEL 1
#endif
#ifndef FEATURE_WIFI_SYNC
#define FEATURE_WIFI_SYNC 1
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
#define FEATURE_SD_FORMAT 1 // SdFat v2 required; destructive operation requires two confirmations
#endif


// 0=S3 builds video feature, 1=omit it entirely on flash-constrained ESP32.
#ifndef EXCLUDE_BAD_APPLE_EASTEREGG
#define EXCLUDE_BAD_APPLE_EASTEREGG (MONOPOD_BOARD == MONOPOD_BOARD_ESP32)
#endif

// MonoPod USB Sync MPOD1 v4: native ESP32-S3 USB CDC (GPIO19 D-, GPIO20 D+).
// USB sync is NOT Apple's iAP and does not implement iTunes pairing.
#ifndef FEATURE_USB_SYNC
#define FEATURE_USB_SYNC (MONOPOD_BOARD == MONOPOD_BOARD_ESP32_S3)
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
