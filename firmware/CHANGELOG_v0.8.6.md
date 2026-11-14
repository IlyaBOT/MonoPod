# v0.8.6 — TFT backlight PWM and fading

- Added physical TFT LED/backlight PWM on ESP32-S3 GPIO15 (LEDC 16 kHz, 10 bit, active-low P-MOSFET high-side by default). The separate TFT reset remains GPIO47.
- Backlight transitions now fade in ~250 ms and out ~400 ms, with gamma-adjusted duty and smoothstep easing; no blocking delay.
- Added `Settings > Backlight` with editable brightness 5–100% (wheel/keys; 5% increments) and timeout 0/5/10/20/30/60 seconds. Preferences persist both settings to NVS.
- `Home > Backlight` manually blanks *the light*, not the TFT pixels. First key press after blank only wakes the screen; music continues playing.
- USB/Wi-Fi Sync always keeps the warning readable and disables auto-backlight timeout. Hardware dimming does not change the SPI framebuffer.
- Classical ESP32/WROVER profile defaults to `PIN_TFT_BL=-1` until a safe dedicated GPIO is chosen. Wokwi profile retains simulated blanking.
- Added native C++ tests of easing monotonicity, as well as Python regression tests; no complete Arduino compile or hardware verification performed.
- Prototype retains TP4056, piezo (no haptic), no development encoder. No Bluetooth Classic on ESP32-S3.
