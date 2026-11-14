# MonoPod v0.7 — font and menu corrections

## Fixed

- Font's glyphs no longer bounce vertically. Both the built-in bitmap tables and the Studio rasterizer use a common text baseline (`ls` rather than individual ink-top `lt`).
- The system header draws a battery pictogram in the upper-right corner. On real hardware it uses GPIO36/ADC1_CH0 with a **100 kΩ + 100 kΩ divider** and optional 100 nF capacitor to ground. Without a valid voltage it displays an unknown state; it does not invent a percentage.
- Wokwi uses `WOKWI_BATTERY_DEMO_PERCENT` (default 75) to preview the battery icon. This is not a simulated real battery.
- The generic footer containing `5 items` was removed from Home, Music and other ordinary list pages. Functional status footers remain in Wi-Fi, storage, Bluetooth and Now Playing screens.
- Titles are clipped before the reserved battery/status area.
- New themes use the baseline-corrected MPT1 flag `1`. Existing themes exported by v0.6 (flag `0`) intentionally fail to load, so the corrected built-in font takes over. Re-export the font in Studio v0.7.

## Not changed

- `Settings → Font Smoothing` defaults to Off; all built-in glyphs remain strictly monochrome bitmap glyphs.
- No extra battery-gauge IC is required; battery percentage is an approximation from voltage.
- Same SPI LCD/audio/SD/key mappings as before.

## Hardware

```
Battery BAT+ ── 100 kΩ ──┬── ESP32 GPIO36 (ADC1_CH0)
                        ├── 100 nF ── GND (optional filtering)
                        └── 100 kΩ ── GND
```

- Do not connect BAT+ directly to GPIO36. ADC must never see the full 4.2 V cell voltage.
- GPIO36 is input-only and conflicts with the optional developer encoder `ENCODER_A` (which is disabled by default). The build now reports that conflict.
- Divider's idle draw at 4.2 V is about 21 µA. In later low-power firmware a gated measurement divider may be useful.

## Verification

- 14 automated tests passed, including theme checksum, baseline alignment, on-screen battery drawing and firmware/Wokwi source parity.
- A static 160×128 preview was rendered and inspected.
- Not yet compiled in the full Arduino-ESP32 3.x toolchain or exercised on a real device/Wokwi browser.
