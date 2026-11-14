# MonoPod UI Demo (ESP8266 + ST7735S)

Standalone display-only Arduino IDE sketch. Displays ten example screens, automatically advancing **every five seconds**: Home, Music, Songs, Now Playing (animated progress and seconds), Settings, Wi-Fi (simulated scan), Bluetooth (simulated device list), Storage, Extras, About. No actual Wi-Fi, Bluetooth, microSD or audio functionality is performed. ESP8266 hardware has no native Bluetooth.

## Wiring

| ST7735S | ESP8266 board | GPIO |
|---|---|---|
| GND | GND | - |
| VCC | 3V3 | - |
| SCL / SCK | D5 | 14 |
| SDA / MOSI | D7 | 13 |
| RES / RST | D0 | 16 |
| DC / A0 | D1 | 5 |
| CS | D2 | 4 |
| BL / LED | 3V3 | - |

Confirm that your module's backlight has a current-limiting resistor and is designed to connect BL to 3V3. No series resistor is assumed on bare LED panels.

## Arduino IDE

1. Install **esp8266 by ESP8266 Community** through Boards Manager.
2. Select the exact ESP8266 development board (e.g. `NodeMCU 1.0 (ESP-12E Module)` or `LOLIN(WEMOS) D1 R2 & mini`).
3. Install **Adafruit GFX Library** and **Adafruit ST7735 and ST7789 Library** through Library Manager. Approve dependencies (Adafruit BusIO).
4. Open `MonoPod_ESP8266_UI_Demo.ino`; upload to ESP8266; open serial monitor at 115200 if desired.

The sketch uses hardware SPI and does not touch SD/USB/ADC inputs. The chosen font is a crisp 1-bit raster from Adafruit GFX `FreeSansBold9pt7b`, not licensed Apple Chicago.

If the panel remains black or the image is offset, test `INITR_GREENTAB` instead of `INITR_BLACKTAB`. If colors are inverted, toggle `tft.invertDisplay()`. For an upside-down image, change `setRotation(1)` to `setRotation(3)`. With unreliable connections, reduce SPI speed to 8 MHz.

## Notes

- The duration on the Now Playing screen is an example, with a seconds counter and a bar based on uptime, not actual audio.
- Battery always shows a demo 76%; no battery is connected or measured.
- Network names, signal strengths and storage figures are simulated.
- Only the **TFT**, no buttons, DAC, battery, or other modules, is required.
