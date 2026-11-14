import sys
from pathlib import Path
import struct
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from convert_bav3 import *

def test_bav3_header():
    assert HEADER.size == 25
    data = HEADER.pack(b'BAV3',160,128,20,4620,145678,1234,1,1,0)
    assert HEADER.unpack(data)[3] == 20
    assert HEADER.unpack(data)[7] == 1

def test_gray6_exact_levels():
    src=bytes([0,85,170,255])*((WIDTH*HEIGHT)//4)
    packed=pack_gray6(src)
    assert len(packed)==15360
    values=unpack_gray6(packed)
    assert values[:4]==bytes([0,21,42,63])
    assert max(values)==63

def test_delta_roundtrip_gray6():
    a=bytes(GRAY_BYTES)
    b=bytes([0,0,0,1,6,0,0,255])*(GRAY_BYTES//8)
    c=bytes([1,1,2,3])* (GRAY_BYTES//4)
    d1=encode_delta(a,b)
    d2=encode_delta(b,c)
    assert decode_delta(a,d1)==b
    assert decode_delta(b,d2)==c
    assert len(d1)<65536

def test_delta_roundtrip_rgb565():
    a=bytes(RGB_BYTES)
    b=bytes((i*13)&255 for i in range(RGB_BYTES))
    d=encode_delta(a,b)
    assert len(d)<65536
    assert decode_delta(a,d)==b

def test_fps_is_per_file():
    for fps in (1,15,20,25,30,60,120):
        data=HEADER.pack(b'BAV3',160,128,fps,1,30,0,0,2,0)
        assert HEADER.unpack(data)[3]==fps
