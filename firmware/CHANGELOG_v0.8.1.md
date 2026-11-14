# v0.8.1 — library sizing and board identification (prototype)

- MAX_TRACKS raised from 350 to 2048 in real and Wokwi configurations.
- Song list UI no longer duplicates every title in its `items` vector; on-screen rows use the indexed metadata.
- Added flash/PSRAM/heap diagnostics on real hardware and media rescan counters.
- Documented TFT RST=-1 and safe external reset wiring for the WROVER pin map.
- Added standalone Arduino `BoardProbe` for identifying 2x USB-C / RGB ESP32 boards.
- ESP32-S3 still NOT supported by the Classic Bluetooth A2DP/AVRCP firmware; no hardware validation.
- Charging architecture remains TP4056 with power off while charging; BQ24074 future PCB only.
