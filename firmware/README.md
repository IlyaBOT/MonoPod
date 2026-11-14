# MonoPod v0.10.0 — MonoPod Desktop protocol candidate

Based on v0.9.1.1. Adds MPOD1 v4 and HTTP filesystem operations, verified streaming transfers, USB theme installation and a 12-color MPT2 palette while retaining MPT1 and legacy sync endpoints. Existing explicit Sync ownership, LAN STA reconnect behavior, stock antialiased font and player geometry are preserved.

Desktop source and protocol documentation: `../MonoPod_Desktop/`. See `CHANGELOG_v0.10.0.md` and `../MonoPod_Desktop/docs/FINAL_REPORT_RU.md`.

Both ESP32-S3 N16R8 and classic ESP32 profiles compiled with Arduino-ESP32 3.3.12. Binaries/manifests are in `../MonoPod_Desktop/firmware-builds/`. Hardware compatibility and power-loss behavior remain UNVERIFIED; no device was flashed. GPIO, AudioEngine, Backlight and Input were not rewritten.

The historical documentation below is retained for reference.

# MonoPod v0.9.1.1 — Wi-Fi Sync over LAN

The player now keeps its existing STA connection to the router when entering Wi-Fi Sync. The HTTP service opens only after `Settings > Wi-Fi > Start Wi-Fi Sync`; it listens on the player's existing IP address on port 80. The Sync screen shows `IP: x.x.x.x`. In MonoPod Studio enter `http://x.x.x.x` (the old `192.168.4.1` is **only for AP fallback**).

When no router association is active and no connection is in progress, Sync starts the old `MonoPod-Sync` softAP. Connect the PC to that Wi-Fi and use `http://192.168.4.1`.

Never expose the HTTP service to the Internet: uploads and settings requests are **unencrypted and unauthenticated**. Use a trusted local LAN or the dedicated offline AP. Synchronization still requires explicit entry into Sync Mode, blocks normal controls and audio, and rejects exit while a file transfer is active.

For details see `CHANGELOG_v0.9.1.1.md` and `docs/WiFi_Sync_LAN_v0.9.1.1_RU.md`.

# MonoPod v0.9.1 — status bar and font smoothing

See `CHANGELOG_v0.9.1.md`. Existing v0.9.0 features and firmware layout remain unchanged.

# MonoPod v0.9.0 — Now Playing / GPU-style offscreen TFT / boot assets / battery

See `CHANGELOG_v0.9.0.md`. Video player uses your own MP4 converted with `tools/convert_badapple.py`. Missing user video is not bundled.

# MonoPod v0.9.0

See `CHANGELOG_v0.8.9.3.md` and `docs/Classic4G_UI_Formatting_Clicker_v0.8.9.3_RU.md` for changes. **Install Greiman SdFat v2** for formatting; test only with backed-up spare cards.

> **v0.8.9.1 hotfix:** Arduino build integration: include WiFiNetworks.h in the physical sketch and implement hasSavedNetwork() in both board/simulator headers. No NVS wipe required.

# MonoPod v0.8.9 — ESP32-S3 Wi-Fi auto-reconnect, signal indicator, UART improvements

See `CHANGELOG_v0.8.9.md` and `docs/MonoPod_v0.8.9_WiFi_and_UART_Diagnostics_RU.md`.

# MonoPod v0.8.7 — UART development console, PWM backlight, explicit USB/Wi-Fi Sync Mode

**MonoPod v0.8.7** uses a **shared, exclusive Sync Mode** for its custom USB CDC protocol and Wi-Fi/HTTP transfer via MonoPod Studio. An ordinary USB connection or Wi-Fi association no longer causes a Sync transition. It does **not** yet emulate a genuine iPod for Apple iTunes.

- Firmware: `DIY_iPod_Mono/DIY_iPod_Mono.ino` (ESP32-S3 N16R8 primary; ESP32 original alternate)
- USB MonoPod Studio sync: `Studio/usb_sync.py`, native S3 USB CDC; see `docs/MonoPod_Sync_Mode_v0.8.5_RU.md`.
- Wi-Fi MonoPod Studio sync: `Settings > Wi-Fi > Start Wi-Fi Sync` on the player; Wi-Fi AP hosted by MonoPod; to exit, **hold Select for 1.5s once upload completes**.
- Fullscreen Sync warning: USB or Wi-Fi icon, large SYNC, do not remove SD or disconnect USB/Wi-Fi during transfer. Playback and ordinary controls are locked.
- Original Apple iPod 3G iTunes research: `docs/CODEX_TASK_iPod3G_USB_iTunes_Reverse_Engineering_RU.md`.
- Existing experimental USB Mass Storage sketch (not integrated into player): `examples/S3_USB_MSC_Lab/`.
- Native USB on ESP32-S3 is **Full-Speed 12 Mbit/s**, not 480 Mbit/s.
- Tests: `python -m pytest -q tests`; `g++ -std=c++17 tests/backlight_math_test.cpp -o /tmp/bltest && /tmp/bltest`. Arduino 3.x / physical hardware integration still needs validation.

## TFT backlight (NEW in 0.8.6)

On ESP32-S3, `Config::PIN_TFT_BL = GPIO15` controls PWM at 16 kHz / 10 bits, with a 250 ms fade-in and 400 ms fade-out. Use an appropriate P-channel high-side MOSFET (active-low gate, `TFT_BL_ACTIVE_LOW=true`), **not** a direct unknown LED current through GPIO. `Settings > Backlight` now offers 5–100% brightness and timeout; settings persist to NVS. Backlight remains on during Sync and goes dark smoothly while music continues. See `docs/MonoPod_Backlight_PWM_v0.8.6_RU.md` for wiring/safety.

**Prototype active-high:** ESP32-S3 GPIO15 now uses `TFT_BL_ACTIVE_LOW=false` for direct GPIO drive. This is a hardware overload risk at 30 mA; use a MOSFET for a reliable build.

**Classic ESP32/WROVER:** PWM GPIO unassigned (`PIN_TFT_BL=-1`) until its wiring is confirmed; S3 is the primary prototype target. Existing Rev A power system remains TP4056.

## Hardware

ESP32-S3 N16R8, ST7735S 160×128, microSD FAT32, PCM5102A, TDA1308 (needs verification), six-pad capacitive click-wheel, button ADC ladder, TP4056 lithium cell charger, audible piezo. No haptic or additional encoder in Rev. A. Future PCB may consider DRV2605L/BQ24074. Bluetooth Classic stack/menu is compiled out on S3.

**Battery safety**: TP4056 has no integrated power-path controller. Charge with player power off in this breadboard configuration. Do not backfeed 3V3 with the DevKit USB supply; verify power isolation before connection.

For older functions see historical `CHANGELOG_*` and `docs/MonoPod_Design_Document_v0.8.3_RU.md`.

## v0.8.7 development console

While Click Wheel hardware is under construction, send `$DISABLE_HARDWARE_CONTROL=1` over **UART0 Serial0** (USB-UART Type-C, 115200, newline) to suspend hardware controls at runtime. Use `$MENU`, `$SELECT`, `$UP`, `$DOWN`, `$SCROLL=DOWN,5`, `$PREV`, `$NEXT`, `$PLAY`, `$STATUS`, `$BATTERY`, `$SD`, `$HELP`. Re-enable with `$DISABLE_HARDWARE_CONTROL=0`. The **native USB CDC is reserved for MPOD1** and must not receive these console commands. Full guide: `docs/MonoPod_UART_Development_Console_v0.8.7_RU.md`.

## v0.8.8 (ESP32-S3 hardware troubleshooting)

ADC and SD wiring faults are resolved. Production firmware mounts microSD **once**, before TFT, at `Config::SPI_SD_HZ` (currently 4 MHz); no automatic format and no 4M/1M/400k fallback. The one-off `$ADC_DIAG` / `$SD_RETRY` diagnostics were removed. Regular battery display and `$BATTERY` / `$SD` status commands remain. For 100k/100k divider, 100 nF GPIO4-to-GND remains recommended.


### v0.8.9.2 production cleanup

- GPIO4 battery: regular ADC voltage/percentage updates remain; fault was disconnected battery ground.
- SD: GPIO14 CS wiring fixed; one mount at 4 MHz via `Config::SPI_SD_HZ` before TFT initialization. No fallback/probing or periodic remounts.
- Removed `$ADC_DIAG` and `$SD_RETRY`; `$BATTERY` and `$SD` are lightweight read-only status commands.
- The separate `MonoPod_S3_DevTest_v1` project remains an independent tool and is not included in the production firmware.
