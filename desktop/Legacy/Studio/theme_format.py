"""MonoPod MPT1 font/theme format, using only user-supplied/local fonts.

28-byte little-endian header: <4sBBBBHHHHHHII>
 magic, version, flags (1=baseline-corrected), bigHeight, smallHeight, bigCount, smallCount,
 bg565, fg565, selection565, muted565, payload_size, crc32(payload)
Followed by two sorted glyph tables. A record is <IBBB>: UTF32 codepoint,
bitmap width, horizontal advance, reserved=0, then tightly packed 4bpp
coverage values, high nibble first, row-major.
All coordinates and glyphs are limited to the player's 160x128 canvas.
"""
from dataclasses import dataclass
from pathlib import Path
import binascii
import struct
from PIL import Image, ImageDraw, ImageFont

HEADER = struct.Struct('<4sBBBBHHHHHHII')
GLYPH = struct.Struct('<IBBB')
CHARS = ''.join(chr(i) for i in range(32, 127)) + ''.join(chr(i) for i in range(0x410, 0x450)) + 'Ёё№'
CHARS = ''.join(sorted(set(CHARS)))
MAX_BYTES = 131072


def rgb565(color):
    c=color.strip().lstrip('#')
    if len(c)!=6:
        raise ValueError('Use hex colors: #RRGGBB')
    r,g,b=(int(c[i:i+2],16) for i in (0,2,4))
    return ((r>>3)<<11)|((g>>2)<<5)|(b>>3)


def from565(value):
    r=((value>>11)&31)*255//31
    g=((value>>5)&63)*255//63
    b=(value&31)*255//31
    return (r,g,b)


@dataclass
class ThemeOptions:
    font_path: str = ''
    main_size: int = 15
    small_size: int = 11
    bg: str = '#ced6c9'
    fg: str = '#202b21'
    selected: str = '#263c29'
    muted: str = '#8b9c86'
    # The device setting controls runtime filtering. Font data always has
    # anti-aliased coverage so OFF/ON does not require re-uploading.
    smoothing: bool = False


def find_default_font():
    candidates = [
        'C:/Windows/Fonts/arialbd.ttf',
        'C:/Windows/Fonts/segoeuib.ttf',
        '/System/Library/Fonts/Supplemental/Arial Bold.ttf',
        '/Library/Fonts/Arial Bold.ttf',
        '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',
    ]
    for candidate in candidates:
        if Path(candidate).is_file():return candidate
    return ''


def font_for(path, size):
    if path:
        return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def raster(char, font, height):
    """Render with ONE baseline shared by every glyph (Pillow 'ls' anchor).

    Old 'lt' aligned glyph ink-top independently, causing M/u/i/g to bounce.
    The baseline stays fixed at y=12 (16px) or y=9 (12px), leaving descent
    space for g/p/q/y and keeping the menu rows visually level.
    """
    advance = max(1, min(32, round(font.getlength(char)) + 1))
    box = font.getbbox(char, anchor='ls')
    width = max(1, min(24, max(advance, box[2], box[2]-min(box[0], 0))))
    im = Image.new('L', (width, height), 0)
    d = ImageDraw.Draw(im)
    baseline = 12 if height == 16 else 9 if height == 12 else max(0, height-4)
    d.text((0, baseline), char, fill=255, font=font, anchor='ls')
    return im, advance


def glyph_table(path,size,height):
    font=font_for(path,size)
    result=bytearray()
    for ch in CHARS:
        bitmap,adv=raster(ch,font,height)
        width,h=bitmap.size
        result.extend(GLYPH.pack(ord(ch),width,adv,0))
        vals=[max(0,min(15,round(v/17))) for v in (bitmap.get_flattened_data() if hasattr(bitmap, 'get_flattened_data') else bitmap.getdata())]
        for i in range(0,len(vals),2):
            result.append((vals[i]<<4)| (vals[i+1] if i+1<len(vals) else 0))
    return result


def encode_theme(opts):
    if not 8<=opts.main_size<=20 or not 8<=opts.small_size<=16:
        raise ValueError('Font sizes out of range')
    payload=glyph_table(opts.font_path,opts.main_size,16)+glyph_table(opts.font_path,opts.small_size,12)
    if len(payload)>MAX_BYTES:raise ValueError('Theme is larger than firmware limit')
    palette=[rgb565(c) for c in (opts.bg,opts.fg,opts.selected,opts.muted)]
    header=HEADER.pack(b'MPT1',1,1,16,12,len(CHARS),len(CHARS),*palette,len(payload),binascii.crc32(payload)&0xffffffff)
    return header+payload


def verify_theme(raw):
    if len(raw)<HEADER.size:raise ValueError('Missing header')
    (magic,version,flags,main_h,small_h,mc,sc,*rest)=HEADER.unpack_from(raw)
    *colors,amount,crc=rest
    if magic!=b'MPT1' or version!=1 or flags!=1 or main_h!=16 or small_h!=12:
        raise ValueError('Incompatible theme header')
    data=raw[HEADER.size:]
    if amount!=len(data) or amount>MAX_BYTES or binascii.crc32(data)&0xffffffff!=crc:
        raise ValueError('Bad length or CRC')
    pos=0
    for count,height in ((mc,16),(sc,12)):
        prior=-1
        if count<1 or count>256:raise ValueError('Bad glyph count')
        for _ in range(count):
            if pos+GLYPH.size>len(data):raise ValueError('Truncated glyph')
            code,w,advance,reserved=GLYPH.unpack_from(data,pos);pos+=GLYPH.size
            if code<=prior or not 1<=w<=24 or not 1<=advance<=32 or reserved:raise ValueError('Invalid glyph')
            pos+=(w*height+1)//2
            if pos>len(data):raise ValueError('Glyph data truncated')
            prior=code
    if pos!=len(data):raise ValueError('Extra bytes in theme')
    return {'main_glyphs':mc,'small_glyphs':sc,'palette':colors,'bytes':len(raw)}


def demo_preview(opts):
    scale=1
    colors=[from565(rgb565(c)) for c in (opts.bg,opts.fg,opts.selected,opts.muted)]
    bg,fg,sel,muted=colors
    im=Image.new('RGB',(160,128),bg)
    d=ImageDraw.Draw(im)
    def text_at(x,y,msg,size,fore,back=None):
        font=font_for(opts.font_path,size)
        for ch in msg:
            bitmap,adv=raster(ch,font,16 if size==opts.main_size else 12)
            if x+adv>160:break
            # Composite simulated AA or crisp black/white into target pixels.
            for yy in range(bitmap.height):
                if y+yy>=128:break
                for xx in range(min(adv,bitmap.width)):
                    if x+xx>=160:break
                    c=bitmap.getpixel((xx,yy))
                    a=(c/255) if opts.smoothing else (1 if c>=128 else 0)
                    base=back if back is not None else im.getpixel((x+xx,y+yy))
                    im.putpixel((x+xx,y+yy),tuple(round(base[i]*(1-a)+fore[i]*a) for i in range(3)))
            x+=adv
    text_at(5,2,'iPod',opts.main_size,fg)
    # 18x9 battery pictogram; this editor does not know the device voltage.
    d.rectangle((136,6,154,14),outline=fg)
    d.rectangle((155,9,156,11),fill=fg)
    d.rectangle((138,8,148,12),fill=fg)  # sample battery level, preview only
    d.line((0,19,159,19),fill=fg)
    entries=('Music','Extras','Settings','Shuffle Songs','Backlight')
    for row,s in enumerate(entries):
        y=21+row*18
        selected=row==0
        if selected:d.rectangle((2,y,157,y+17),fill=sel)
        text_at(6,y+1,s,opts.main_size,bg if selected else fg,sel if selected else bg)
        if row<3:
            x=151
            d.polygon(((x,y+6),(x+5,y+9),(x,y+12)),fill=bg if selected else fg)
    # No meaningless "5 items" status bar in the main menu.
    return im
