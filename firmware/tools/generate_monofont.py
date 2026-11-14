#!/usr/bin/env python3
"""Generate compact 4-bit-alpha MonoPod bitmap tables from DejaVu Sans Bold.

The original DejaVu TTF is NOT bundled. Requires Pillow and a locally installed
DejaVu Sans Bold font. Font license text is kept in LICENSE-MONOFONT.txt.
"""
from __future__ import annotations
import argparse
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import re

SCALE = 8
ROOT = Path(__file__).resolve().parents[1]
FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'


def source_codepoints(original: str):
    tables = {}
    for name in ('big', 'small'):
        table = original.split(f'static const Glyph {name}[] PROGMEM = {{', 1)[1].split('};', 1)[0]
        old = re.findall(r'\{(\d+),\d+,\d+,\{', table)
        modern = re.findall(r'\{(\d+),\d+,\d+,\d+\}', table)
        tables[name] = [int(p) for p in (old if old else modern)]
        if not tables[name]:
            raise ValueError(f'No characters found in the {name} font')
    return tables


def render_font(font_path: str, cps: list[int], size: float, baseline: float, height: int):
    font = ImageFont.truetype(font_path, round(size * SCALE))
    glyphs, pixels = [], []
    for cp in cps:
        ch = chr(cp)
        # Preserve proportional advances, but ensure the rightmost antialiased
        # edge is never clipped. Text never gets scaled at display time.
        text_advance = font.getlength(ch) / SCALE
        bb = font.getbbox(ch, anchor='ls')
        # Negative left-bearing is uncommon (e.g. j); reserve one extra pixel.
        shift = max(0, (-bb[0] + SCALE-1) // SCALE)
        width = max(2, int(text_advance + 0.75) + shift)
        width = max(width, (bb[2] + SCALE-1) // SCALE + shift)
        if width > 31:
            raise ValueError(f'Glyph too wide: U+{cp:04X} {width}')
        pic = Image.new('L', (width * SCALE, height * SCALE), 0)
        d = ImageDraw.Draw(pic)
        d.text((shift * SCALE, round(baseline * SCALE)), ch, font=font, fill=255, anchor='ls')
        img = pic.resize((width, height), Image.Resampling.LANCZOS)
        alpha = [min(15, max(0, round(v / 17))) for v in img.get_flattened_data()]
        start = len(pixels)
        # First alpha at high nibble. Offset is in BYTES and each glyph starts
        # at a new byte, so odd pixel counts have one padding nibble.
        for i in range(0, len(alpha), 2):
            pixels.append((alpha[i] << 4) | (alpha[i+1] if i+1 < len(alpha) else 0))
        glyphs.append((cp, width, width, start))
    return glyphs, pixels


def format_array(name, data):
    out = [f'static const uint8_t {name}[] PROGMEM = {{']
    for i in range(0, len(data), 20):
        out.append('  ' + ','.join(f'0x{v:02x}' for v in data[i:i+20]) + ',')
    out.append('};')
    return '\n'.join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--font', default=FONT)
    ap.add_argument('--previous', default=str(ROOT/'DIY_iPod_Mono'/'MonoFont.h'))
    ap.add_argument('--output', default=str(ROOT/'DIY_iPod_Mono'/'MonoFont.h'))
    args = ap.parse_args()
    original = Path(args.previous).read_text(encoding='utf-8')
    cps = source_codepoints(original)
    big, bp = render_font(args.font, cps['big'], 14.5, 12.0, 16)
    small, sp = render_font(args.font, cps['small'], 11.5, 9.5, 12)
    output = ['''#pragma once
#include <Arduino.h>
#include <pgmspace.h>
// Generated from DejaVu Sans Bold (Bitstream Vera license, see LICENSE-MONOFONT.txt).
// Each glyph uses an independent fixed-height alpha4 raster. The OFF setting
// thresholds this SAME raster, so anti-aliasing OFF/ON does not change layout.
// Source generator: tools/generate_monofont.py
namespace MonoFont {
struct Glyph {uint16_t codepoint;uint8_t advance;uint8_t width;uint32_t offset;};''']
    for name, entries, pix in [('big', big, bp), ('small', small, sp)]:
        output.append(f'static const Glyph {name}[] PROGMEM = {{')
        for cp,adv,w,off in entries:
            output.append(f'  {{{cp},{adv},{w},{off}}},')
        output.append('};')
        output.append(f'static constexpr size_t {name}Count=sizeof({name})/sizeof({name}[0]);')
        output.append(format_array(name+'Pixels',pix))
    output.append('''inline uint8_t alpha(const Glyph& g,bool small,uint8_t x,uint8_t y){
  const uint8_t height=small?12:16;
  if(x>=g.width || y>=height) return 0;
  const size_t index=(size_t)y*g.width+x;
  const uint8_t* table=small?smallPixels:bigPixels;
  uint8_t packed=pgm_read_byte(table+g.offset+(index>>1));
  return (index&1)?(packed&15):(packed>>4);
}
}''')
    Path(args.output).write_text('\n'.join(output)+'\n',encoding='utf-8')
    print(f'Generated {len(big)} big and {len(small)} small glyphs, {len(bp)+len(sp)} alpha bytes')

if __name__=='__main__': main()
