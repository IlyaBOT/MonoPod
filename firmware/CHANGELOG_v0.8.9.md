# MonoPod v0.8.9 — Wi-Fi reconnect, signal icon, development console fixes

## New
- On every boot `WiFiNetworkManager::begin()` reads the last successfully connected SSID/password from the existing `monopod-wifi` NVS namespace and begins an asynchronous STA connection. No need to visit Wi-Fi Networks.
- STA reconnect after link loss with a 15-second retry interval, 16-second per-attempt timeout; no blocking wait in UI loop.
- Wi-Fi AP Sync suspends STA; after Sync exit the saved STA connection is restored. USB connection alone does not enter Sync.
- Status bar uses 1/2/3 signal bars (RSSI sampled every 12 seconds). Hidden if disconnected. During connection it blinks every 650 ms. Only the status icon region is repainted, avoiding whole-screen flicker.
- `$WIFI_STATUS` for observing STA/NVS state without printing the saved password.

## Bug fixes
- `$DISABLE_HARDWARE_CONTROL=1` now disables hardware polling and silences an *existing* click, but does **not** permanently mute virtual UART clicks. `$CLICKER_MUTE` is independent. `$CLICKER=1/0` is a convenience alias for sound enabled/disabled.
- `$ADC_DIAG` reports GPIO4 ADC raw values and calibrated millivolts at four attenuation settings, then restores 11dB. A ~500 mV reading while 1.97 V is measured by voltmeter is **not** considered fixed and is not scaled artificially.
- The SD error is **not** hidden: `SD=NOT_MOUNTED`, `SPI_HZ=0` mean no SPI mount succeeded. Formatting is intentionally disabled. Use standalone non-destructive SD diagnostic in `examples`.
- Both physical sketch and Wokwi source copies updated.

## Validation
- 51 Python tests passed and native C++ parser + signal math tests passed in this environment.
- **Not performed:** full compile with Arduino-ESP32 3.3.12, actual ESP32-S3 tests, microSD hardware test, ADC readback, I2S clicker playback. Treat this version as an unverified hardware candidate.
