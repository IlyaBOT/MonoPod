# MonoPod v0.8-rc — integration candidate (2026-10-28)

Based on v0.7 supplied in the handoff archive. This is an integration candidate, NOT a claimed successful Arduino firmware release.

## Changes

- Fixed `BluetoothOutput.cpp` compile-guard scope: every class method remains inside `#if FEATURE_BLUETOOTH`.
- Input: a 35 ms debounced Select short-click now fires on release; long press at >=1500 ms emits `Key::SelectLong` exactly once. No extra Select on long-release.
- Bluetooth menu split into `Paired Devices` and `Scan and Pair`.
- Paired devices: Classic BT bond list from ESP-IDF, optional name cache in NVS `bt-labels`, Select connect/disconnect and long-Select with erase confirmation.
- Scan: Classic inquiry, de-duplication/selection by six-byte Bluetooth address, stale scan filtering.
- Pairing: intercept actual GAP legacy PIN / SSP passkey / numeric comparison requests using a subclass of BluetoothA2DPSource, so the existing A2DP library still owns its GAP callback.
- AVRCP passthrough: Play/Pause/Next/Previous routed through a nonblocking FreeRTOS queue; Play/Pause semantics separated to prevent toggling into the wrong state.
- Fixed Wokwi sketch duplicate `.ino` setup()/loop(): only `sketch.ino` remains.
- Added standalone engineering design document and native C++ long-Select input regression.

## Tests performed in this environment

- `python -m pytest -q tests` => 17 passed (Studio, UI static checks and theme tests).
- Native Wokwi-profile `g++` Input.cpp executable => PASS short select / long select / no duplicate / Previous.
- Physical ST7735S, SPI/SD, DAC, A2DP, AVRCP, charging: **not tested**.
- **No Arduino-ESP32 complete compile** performed (Arduino CLI and installed embedded libraries unavailable).

## Critical next step

Compile `DIY_iPod_Mono` with Arduino-ESP32 3.x twice: `FEATURE_BLUETOOTH=0` then `1`. Resolve any ESP32-A2DP version-specific Source method signatures first. Verify actual GAP PIN/SSP behavior and A2DP/AVRCP with a real ESP32-WROVER and headset before setting FEATURE_BLUETOOTH=1 by default.

## Documentation update: TP4056 breadboard power (28 Oct 2026)
- Updated prototype BOM, architecture and installation/test procedure for TP4056.
- BQ24074 moved to prospective custom-PCB Rev. B only; no firmware changes.
- Added explicit no-load-while-charging and DC/DC / battery-protection requirements.
