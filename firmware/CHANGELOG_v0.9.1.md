# MonoPod v0.9.1 — Status bar / stock font rasterization

## Changes
- The centered status-bar title is drawn **1 display pixel lower** (`y=3` instead of `y=2`). HOLD text moves with it (`y=4`). The battery and playing icons are not displaced.
- Wi-Fi signal bars are drawn **1 display pixel higher** (`OY+15-height` instead of `OY+16-height`), including partial indicator refreshes.
- Rebuilt the stock DejaVu Sans Bold-derived big/small glyphs at high resolution with a common baseline and 4-bit coverage values. This reduces lost edges and uneven thickness on `u, d, l, i, t, f, s, g`, and preserves proportional spacing and Cyrillic coverage.
- **Font Smoothing** now works for the built-in font, not only for third-party `.mpt` theme glyphs. `Off` uses a sharp 1-bit threshold of the same glyph; `On` blends with 16 transparency levels. Settings still persist in NVS under `fontAA`, and redraw immediately upon returning to the menu.
- Synced both firmware and Wokwi copies, added font table bounds/coverage tests and a native C++ glyph-decode test.

## Compatibility
- No wiring or partition changes, no NVS schema change. Settings default stays **OFF** for pixel-crisp display.
- Font assets are generated from DejaVu Sans Bold under `LICENSE-MONOFONT.txt`; a reproducible generator is in `tools/generate_monofont.py`. The source TTF is NOT redistributed.
- All text uses RGB565 alpha blending in the configured UI palette. On hardware, subjective appearance must be checked on the ST7735S: the 160x128 pixel grid limits how uniform very small glyphs can look.
