"""Regression checks for status-bar alignment and stock font AA in v0.9.1."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
FW = ROOT / 'DIY_iPod_Mono'


def test_statusbar_offsets_and_wokwi_sync():
    physical = (FW / 'Display.cpp').read_text(encoding='utf-8')
    simulated = (ROOT / 'Wokwi_ILI9341' / 'Display.cpp').read_text(encoding='utf-8')
    assert physical == simulated
    assert 'drawString(x,3,fitted,fg,bg,false,0,134)' in physical
    assert 'if(held)smallText(20,4,"H",fg,bg)' in physical
    assert 'OY+15-height' in physical


def font_tables():
    h = (FW / 'MonoFont.h').read_text(encoding='utf-8')
    result = {}
    for name, height in (('big', 16), ('small', 12)):
        glyph_text = h.split(f'static const Glyph {name}[] PROGMEM = {{', 1)[1].split('};', 1)[0]
        byte_text = h.split(f'static const uint8_t {name}Pixels[] PROGMEM = {{', 1)[1].split('};', 1)[0]
        entries = [(int(cp), int(advance), int(width), int(offset))
                   for cp, advance, width, offset in
                   re.findall(r'\{(\d+),(\d+),(\d+),(\d+)\}', glyph_text)]
        binary = [int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', byte_text)]
        result[name] = (height, entries, binary)
    return result


def test_alpha4_tables_preserve_coverage_and_bounds():
    for name, (height, entries, raw) in font_tables().items():
        assert len(entries) >= 160, name
        assert entries == sorted(entries), name
        assert len({cp for cp, *_ in entries}) == len(entries)
        for cp, adv, width, offset in entries:
            assert 0 < adv <= 31 and width == adv
            assert 0 <= offset < len(raw)
            assert offset + (width * height + 1)//2 <= len(raw), (name, cp)
        # Intermediate alpha levels prove that the stock font actually contains
        # antialiasing information (unlike the old monochrome font).
        alphas = [(b >> 4) for b in raw] + [(b & 15) for b in raw]
        assert 0 in alphas and 15 in alphas
        assert any(0 < a < 15 for a in alphas)
        codes = {cp for cp, *_ in entries}
        for ch in 'udlitfsg?ЩЁ№':
            assert ord(ch) in codes


def test_default_font_respects_smoothing_setting():
    display = (FW / 'Display.cpp').read_text(encoding='utf-8')
    ui = (FW / 'UI.cpp').read_text(encoding='utf-8')
    settings = (FW / 'Settings.cpp').read_text(encoding='utf-8')
    assert 'MonoFont::alpha(g,small,xx,yy)' in display
    assert 'if(!settings.data.fontSmoothing)alpha=(alpha>=8)?15:0;' in display
    assert 'blend565(back,front,alpha)' in display
    assert 'settings.data.fontSmoothing=(selected==1)' in ui
    assert 'pref.getBool("fontAA",false)' in settings
    assert 'pref.putBool("fontAA",data.fontSmoothing)' in settings
    assert (FW / 'MonoFont.h').read_bytes() == (ROOT / 'Wokwi_ILI9341' / 'MonoFont.h').read_bytes()
