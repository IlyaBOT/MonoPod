"""Regression tests for root causes from the October 26 screen report."""
import sys
from pathlib import Path
from PIL import ImageFont
ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'Studio'))
from theme_format import raster, ThemeOptions, encode_theme, verify_theme, demo_preview, find_default_font

def occupied_rows(im):
    def pixels(y):
        row=im.crop((0,y,im.width,y+1))
        return row.get_flattened_data() if hasattr(row,'get_flattened_data') else row.getdata()
    return [y for y in range(im.height) if max(pixels(y))>127]

def test_shared_baseline_for_main_text():
    font=ImageFont.truetype(find_default_font(),15)
    M=occupied_rows(raster('M',font,16)[0]);u=occupied_rows(raster('u',font,16)[0]);g=occupied_rows(raster('g',font,16)[0]);i=occupied_rows(raster('i',font,16)[0])
    assert max(M)==max(u)==max(i)==11, (M,u,i)
    assert max(g)>max(u), (g,u)
    assert min(M)<min(u) and min(i)<min(u)

def test_shared_baseline_for_small_text():
    font=ImageFont.truetype(find_default_font(),11)
    upper=occupied_rows(raster('M',font,12)[0]);low=occupied_rows(raster('u',font,12)[0]);desc=occupied_rows(raster('g',font,12)[0])
    assert max(upper)==max(low)==8
    assert max(desc)>max(low)

def test_battery_and_empty_lower_margin_in_preview():
    opts=ThemeOptions(font_path=find_default_font())
    im=demo_preview(opts)
    bg=im.getpixel((0,118))
    assert all(im.getpixel((x,y))==bg for y in range(114,128) for x in range(160)), 'footer must remain empty'
    assert im.getpixel((136,6))!=im.getpixel((130,6)), 'battery outline is visible'
    verify_theme(encode_theme(opts))

def test_hardware_wokwi_parity():
    fw=ROOT/'DIY_iPod_Mono';sim=ROOT/'Wokwi_ILI9341'
    for name in ['Display.cpp','Display.h','UI.cpp','Battery.cpp','Battery.h','MonoFont.h']:
        assert (fw/name).read_bytes()==(sim/name).read_bytes(),name
    for target in (fw,sim):
        assert '5 items' not in (target/'UI.cpp').read_text()
        assert 'items.size())+" items"' not in (target/'UI.cpp').read_text()

def test_reject_old_misaligned_theme_flag():
    opts=ThemeOptions(font_path=find_default_font())
    raw=bytearray(encode_theme(opts))
    assert raw[5]==1
    raw[5]=0 # legacy v0.6: old glyphs have incorrect top-only alignment
    import pytest
    with pytest.raises(ValueError):verify_theme(raw)
