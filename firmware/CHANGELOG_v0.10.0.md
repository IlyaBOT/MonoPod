# MonoPod v0.10.0 — Desktop integration

Base: the verified v0.9.1.1 tree. This revision is separate; all previous revisions are preserved.

- Shared SyncFiles SD operations with explicit USB/Wi-Fi ownership, paginated LIST, STAT/META, bounded READ with CRC, upload, mkdir, empty-only rmdir, rename, delete and 64-bit storage values.
- MPOD1 v4 adds capability negotiation and hexadecimal UTF-8 paths while retaining legacy HELLO/INFO/SYNC/PUT/REPLACE/DELETE/SCAN. Native CDC stays separate from the UART console.
- Wi-Fi adds matching `/api/fs/*` operations, strict size/CRC multipart uploads and bounded downloads with X-CRC32. Existing STA/AP behavior and old Studio routes remain.
- Streaming temporary file, length/CRC validation, backup/target journal, rollback on recoverable failures and boot recovery. Index refresh follows recovery when required. No claim of arbitrary physical FAT power-loss atomicity is made.
- USB and Wi-Fi install/read themes. MPT2 extends the palette to 12 RGB565 colors; MPT1 stays supported. Strict length/version/scalar/glyph/CRC validation precedes activation. Smoothing remains runtime-selectable.
- Main and Wokwi shared source variants updated. Stock v0.9.1 font/geometry and v0.9.1.1 LAN reconnect retained. GPIO, AudioEngine, Input and Backlight remain unchanged from the baseline.

## Build and evidence

Arduino-ESP32 3.3.12, both profiles successful:

| Board | Program bytes | Static RAM bytes |
|---|---:|---:|
| ESP32-S3 N16R8 | 1,454,610 | 63,020 |
| ESP32 Classic / huge_app | 1,455,019 | 58,428 |

S3 excludes Bluetooth Classic at compile time. Classic ESP32 uses Wi-Fi, since it has no native USB CDC. Firmware artifacts and individual SHA-256 manifests: `../MonoPod_Desktop/firmware-builds/`.

Desktop and native software tests are documented in `../MonoPod_Desktop/docs/VALIDATION_RU.md`. Physical USB, playback, SD card, heap, theme appearance and power interruption tests remain UNVERIFIED. No flashing, formatting or partition changes were performed.


## Boot regression fix inherited from v0.9.0

Moved the 4096-byte MP3 probe window from recursive stack to bounded RAII heap allocation, with a safe unknown-duration fallback on allocation failure. Exact crash ELF showed 4416-byte recursive frames on an 8192-byte loop stack; final S3/classic frames are 352 bytes. Added UART boot stages and stack low-water reporting after indexing. Both profiles rebuilt; real boot remains UNVERIFIED. Details: `../MonoPod_Desktop/docs/BOOT_REGRESSION_RU.md`.
