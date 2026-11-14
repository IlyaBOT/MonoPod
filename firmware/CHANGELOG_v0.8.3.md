# v0.8.3 — S3 Native USB Sync / wiring revision

- Removed unused experimental 30-pin iAP1 RX UART receiver and all references to `PIN_IAP1_RX`; GPIO41 is **not** used for USB sync.
- Introduced ESP32-S3 native USB CDC file transfer protocol `MPOD1`, writing music to microSD in 1024-byte flow-controlled chunks with full-file CRC32 verification and `.part` rollback.
- Implemented CLI `Studio/usb_sync.py` and Studio GUI USB status/music upload controls; optional `pyserial` added to requirements.
- Redirected diagnostics to `Serial0` on S3 so no logs contaminate USB binary transfer protocol.
- Fixed duplicate C++ `ENCODER_A/B` and `PIN_IAP1_RX` definitions present in v0.8.2 `Config.h`.
- Documentation now targets user's ESP32-S3 N16R8 breadboard prototype: TP4056, audio piezo clicker; no haptic actuator or extra encoder. Future DRV2605L is Rev. B only.
- ESP32 original profile keeps existing features; Bluetooth Classic remains absent from S3 firmware.
- USB sync is **not** iTunes/iAP/MSC. USB code has not been compiled against real Arduino-ESP32 toolchain or tested on target hardware.
