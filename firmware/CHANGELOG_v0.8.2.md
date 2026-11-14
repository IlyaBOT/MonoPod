# MonoPod v0.8.2 — ESP32 / ESP32-S3 build profiles

- `MONOPOD_BOARD` (automatic from Arduino's selected target, or explicit classic/S3).
- Separate safe S3 N16R8 GPIO wiring; ST7735 reset now dedicated GPIO47 on S3.
- Compile-time elimination of Bluetooth Classic A2DP/AVRCP for ESP32-S3 (including headers, globals, UI settings, pairing state and audio switch paths). Explicit build error when someone forces Bluetooth on S3.
- Settings now dispatch by persistent item `.id`, not the menu row index, preventing selection shifts when feature items are conditionally hidden.
- Touch wheel now interprets **rising** sensor readings as touch on S3, **falling** readings on classic ESP32; buffers widened to 32 bits.
- Retains library limit of 2048 tracks and TP4056 prototype charger.
- Historical 0.8.1 PDF/DOCX are included unchanged; the supplemental S3 profile document supersedes their pin tables for S3.
- Hardware, full Arduino build and resulting binary size remain unverified.
