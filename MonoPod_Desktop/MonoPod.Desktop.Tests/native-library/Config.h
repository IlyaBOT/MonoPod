#pragma once
#include "Arduino.h"
#define WOKWI_SIMULATION 1
#define FEATURE_SD_FORMAT 0
#define FEATURE_BLUETOOTH 0
namespace Config {constexpr size_t MAX_TRACKS=2048;constexpr uint8_t MAX_SCAN_DEPTH=6;constexpr int PIN_TFT_CS=10,PIN_SD_CS=14;constexpr uint32_t SPI_SD_HZ=4000000;}
