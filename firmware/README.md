# MonoPod v0.6 + Studio — theme editing and device management

**Targets:** ESP32-WROVER, ST7735 160×128 TFT, microSD, PCM5102A, Arduino-ESP32 3.x; desktop editor for Windows, macOS and Linux.

## What is new

- `Settings → Font Smoothing → Off / On`. **Off by default**, saved as `fontAA` in Preferences/NVS. With smoothing disabled, every glyph pixel is either the foreground or background color; no intermediate shades.
- Built-in fallback 1-bit font remains in `MonoFont.h` (a bold, open-license bitmap derived from DejaVu Sans, NOT the proprietary Chicago font). Fallback font remains crisp even when an imported theme is unavailable.
- Optional imported fonts and four-color palettes via microSD `**/MonoPod/theme.mpt**`. Runtime glyph coverage is 4-bit; choosing Off applies a binary threshold and On blends 16 coverage levels. Both can be used without reflashing.
- **MonoPod Studio** desktop app creates theme bitmaps from a font selected *locally* (TTF/OTF supported by Pillow), previews at the true 160×128 resolution, exports `.mpt`, and can sync theme, font smoothing and music over the player's local Wi-Fi hotspot. No fonts are bundled. `.dfont` generally needs a local conversion to TTF/OTF if the font license permits it.

## Start the desktop editor

**Windows:** double-click `Studio/run_windows.bat` (Python 3.10+ recommended).

**macOS / Linux:** from a shell:

```bash
cd Studio
python3 -m pip install -r requirements.txt
python3 monopod_studio.py
```

The app requires Python with Tkinter and Pillow; on minimal Linux distros, install `python3-tk` separately. For offline work, no connection to the ESP32 is necessary.

1. Browse to a **locally licensed** TTF/OTF (e.g., your own Chicago font). No font bytes are uploaded to third-party services.
2. Choose menu size, label size, four theme colors, smoothing mode.
3. Use the live pixel-accurate preview. Save `monopod-theme.mpt` with **Export theme**. The local JSON profile records the font file's local path and theme settings, *not* the font data.
4. Copy `monopod-theme.mpt` to `microSD:/MonoPod/theme.mpt`, creating `/MonoPod` if necessary, and restart the player. Alternatively, upload using MonoPod Studio over Wi-Fi.

**Licensing:** If your Chicago license permits installation on your own hardware, importing it locally is technically supported. Distribution of any exported theme embedding its glyphs may still be restricted by that license; review its terms. We distribute no original Chicago font or bitmaps.

## Flash the device firmware

1. Open `DIY_iPod_Mono/DIY_iPod_Mono.ino` in Arduino IDE.
2. Install ESP32 by Espressif Systems 3.x (board **ESP32 Wrover Module**, PSRAM enabled where available).
3. Install `Adafruit GFX Library`, `Adafruit ST7735 and ST7789 Library`, `ESP8266Audio`; optional ESP32-A2DP only if Bluetooth is enabled. See existing `Config.h` flags.
4. Compile and upload. Insert microSD; optional theme in `/MonoPod/theme.mpt`.

Wokwi source profile is in `Wokwi_ILI9341/` (simulation on ILI9341; native real-screen build uses ST7735). Custom fonts are loaded only if the simulated SD contains the theme file; otherwise the built-in fallback is shown. Wokwi virtual Wi-Fi Sync and Bluetooth are not hardware substitutes.

## Wi-Fi device management

The original ESP32-WROVER uses USB-UART, not USB Mass Storage. Use Wi-Fi for transfer:

1. On the player select `Settings → Wi-Fi → Start Wi-Fi Sync`.
2. On your PC connect to Wi-Fi **MonoPod-Sync**, using the AP password specified in `Config.h`. **Change this default password before everyday use.**
3. In MonoPod Studio, select **Device & Music** and connect to `http://192.168.4.1`.
4. Use **Generate & Upload current theme**, **Apply smoothing setting**, **Restore built-in theme**, or **Select music files and upload** (uploads MP3/WAV/FLAC/AAC to `/Music` one by one).

All connections are **unencrypted HTTP on a local wireless network**; the prototype has no separate HTTP login. Do not expose port 80 to the public Internet or untrusted LAN users. Uploads are restricted by the wireless hotspot credentials, not an application API token.

### Device API

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/info` | GET | Firmware version, track count, card storage in MiB, theme and font smoothing state |
| `/api/theme` | POST `multipart/form-data` | Upload `.mpt` as field `theme`; CRC check, backup and atomic rename where supported |
| `/api/smoothing` | POST form `enabled=0/1` | Update persisted smoothing setting |
| `/api/theme/reset` | POST | Delete custom theme from SD and revert immediately to built-in font |
| `/upload` | POST multipart field `track` | Existing legacy upload of audio to `/Music` |

Installed themes are stored as plain `/MonoPod/theme.mpt` on microSD and loaded into RAM on boot; **even if removed or invalid, the built-in font remains usable**. A file uploaded by Studio is not the licensed original TTF file—it is a rasterized subset including printable Latin, basic Cyrillic, Ё/ё and №.

### Binary theme format

See `Studio/theme_format.py` for the authoritative MPT1 encoder and `DIY_iPod_Mono/Theme.cpp` for the C++ parser. Header is 28 bytes, little-endian; payload is two sorted glyph tables with UTF-32 codepoints, fixed line heights 16 and 12, packed 4-bit alpha masks, maximum 128 KiB of payload and CRC32 validation. 4-color theme palette is stored as RGB565.

**The default renderer is 1-bit regardless of TFT hardware color depth.** The extra 4-bit alpha values are applied only when Font Smoothing is On.

## Source organization

- `Studio/monopod_studio.py` — desktop GUI/theme preview and device controls.
- `Studio/theme_format.py` — glyph rasterizer + MPT1 binary writer/validator.
- `Studio/device_api.py` — local HTTP interface/streaming uploads.
- `DIY_iPod_Mono/Theme.cpp/.h` — optional font/theme loader in ESP32 RAM.
- `DIY_iPod_Mono/Display.cpp` — rendering with binary/4-bit alpha switch.
- `DIY_iPod_Mono/UI.cpp`, `Settings.cpp` — `Font Smoothing` menu and NVS.
- `DIY_iPod_Mono/WiFiSync.cpp` — REST endpoints for Studio.
- `tests/` — Python tests and portable C++ parser sanity check.
- `examples/mono-classic-demo.mpt` — example bitmap theme derived from DejaVu Sans Bold, with license in `DIY_iPod_Mono/LICENSE-MONOFONT.txt`. **No font file is bundled.**

## Tests and limitations

- Python unit tests cover binary encoding/validation, CRC errors, font sizes, black-and-white versus smoothed preview.
- The C++ `Theme.cpp` parser was compiled **outside** the Arduino environment with stub SD functions and successfully read an exported `.mpt` including Latin/Cyrillic; it rejected corrupt CRC.
- The desktop GUI was started using Xvfb for a smoke test on Linux.
- **No successful full ESP32 Arduino compilation, physical board test, or over-Wi-Fi end-to-end upload test has been performed here.** Hardware-specific behavior remains unverified.
- Retains previous v0.5 experimental Bluetooth, formatting, and audio capabilities and their caveats.
- iTunes sync, genuine Apple ROM/UI/font, Apple proprietary assets and complete iAP have **not** been added.

## License

The existing project contains `COPYING` (GPLv3) and `LICENSE-MONOFONT.txt` for the derived built-in bitmap font. Any exported custom theme is your own responsibility under its original typeface license. No third-party font binaries are shipped in this release.
