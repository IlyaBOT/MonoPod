# MonoPod v0.9.0 — инструкция для ESP32-S3 N16R8

## Arduino IDE
- Board: ESP32S3 Dev Module
- Flash: 16MB, PSRAM: OPI PSRAM
- USB CDC On Boot: Enabled (MonoPod MPOD1)
- UART0/Serial0: debug console 115200, via separate USB-UART port
- Partition: **16M Flash (3MB APP/9.9MB FATFS)** for normal firmware. Video embedded into the executable may exceed 3MB; choose larger APP partition where available and ensure build fits. Do not assume 16MB Flash means 16MB usable APP space.
- Required libraries unchanged: ESP8266Audio, Adafruit GFX/ST7735; SdFat v2 for format. Neither MP4 playback nor an AAC/ALAC decoder is needed for Bad Apple BAV1.

## Backlight / hardware safety
- GPIO15 `TFT_BL_ACTIVE_LOW=false`: *active-high* PWM for direct LED/BL input in current prototype.
- A measured 30mA direct load **is not recommended** on an ESP32-S3 GPIO. Use an external MOSFET switch to prevent pin damage. Set `TFT_BL_ACTIVE_LOW=true` if using an active-low P-MOSFET gate.
- PCM5102A analog line output is not a headphone amplifier. Driving 4Ω headphones directly can distort or damage the DAC; use a headphone amplifier with suitable output current and protection.

## Now Playing
- Wheel UP/DOWN: volume 0–100, clamped, 1% if slow and 10% when adjacent wheel events are <=110ms apart.
- SELECT: enter Seek, wheel adjusts scrub; seek is applied after 350ms quiet, or when SELECT is pressed again. Five seconds idle -> progress bar.
- Non-audio buttons (Menu/Prev/Next/Select) have no synthesized scroll-click. Hardware clicker only accompanies UP/DOWN events.
- Time display updates when elapsed wall-clock seconds change; metadata stays drawn. Left is elapsed; right is remaining if a duration could be determined.
- Song number counts the current position in the entire sorted library (not a separate playlist index). Track total is library count.
- Duration accuracy: TLEN and Xing/Info MP3 frames; CBR MP3 bitrate estimate; simple RIFF/WAV header. VBR without Xing and AAC/FLAC may show an estimate or `--:--`. MP3 seek is byte/frame approximation; it is not sample-accurate. Original iPod-level exact seek requires an indexed decoder.

## Flicker
On S3 a `GFXcanvas16` frame buffer (160 x 128 x 2 = **40960 bytes**) is rendered offscreen then transferred in one TFT operation. When memory is exhausted, the firmware falls back to drawing directly. The Now Playing progress/volume/time block is refreshed independently. Backlight wake does not force a full-screen clear. Confirm smooth behavior on physical ST7735S; SPI speed and library settings matter.

## Battery
Voltage sampled at boot and every 60 seconds; the battery icon is 0–4 bars with band thresholds at 0/25/50/75%. Repainting occurs only if the band changes. Actual SoC is estimated from Li-ion terminal voltage, not measured by fuel gauge.

## Boot image / theme
- Default: bundled monochrome pixel Apple-style mark on black.
- Override: put `/MonoPod/boot.rgb565` on the card, **exactly 40960 bytes**, width 160, height 128, RGB565 little-endian. Color is supported.
- Convert on PC:
  ```powershell
  py -m pip install pillow
  py tools\pack_bootlogo.py mylogo.png boot.rgb565
  ```
- The supplied symbol is a clean raster recreation, not a byte-exact extraction from Apple's firmware.

## Bad Apple
- `#define EXCLUDE_BAD_APPLE_EASTEREGG (MONOPOD_BOARD == MONOPOD_BOARD_ESP32)` defaults to 1 (excluded) on original ESP32, 0 on S3. It compiles out all player code on classic ESP32.
- Convert **your own** MP4 using ffmpeg installed in PATH:
  ```powershell
  py tools\convert_badapple.py "C:\Videos\BadApple.mp4" badapple.bav --fps 15
  ```
  Resolution 160x128, 4 grayscale levels (2 bits/pixel), XOR/delta RLE, nominal 15fps (supports up to 25, depending on SPI throughput). No audio is extracted.
- Copy `badapple.bav` to microSD `/MonoPod/badapple.bav`. Menu: Extras → Bad Apple. MENU exits.
- **Optional Flash embedding:** regenerate with `--embed-header DIY_iPod_Mono/BadAppleEmbedded.h`. This compiles compressed frames as PROGMEM instead of reading them from SD. It will require enough APP partition space and may not fit 3MB. Flash usage is NOT automatically expanded.
- **No source MP4 available in this working session**, so no copyrighted video frames or audio are bundled with v0.9.0. The player will report missing video until the asset is added.

## Diagnostics
1. Verify boot screen and no reversed PWM brightness.
2. Check Now Playing with MP3 having TLEN/Xing, then WAV, and confirm elapsed/remaining time on one-second boundaries.
3. Open volume, turn slowly/quickly, verify hard 0 and 100 limits.
4. SELECT -> seek; test short and long songs, including mono/VBR MP3. Seek remains experimental on ESP8266Audio.
5. Observe status battery icon for 1-minute samples and an actual band transition.
6. Video test with short synthetic BAV before attempting a full video.
7. If low memory causes direct redraw fallback, inspect logs for `TFT: framebuffer unavailable`.

**Validation:** static and Python regression suite completed; full Arduino-ESP32 3.3.12 compilation and real-device performance/seek tests remain pending.
