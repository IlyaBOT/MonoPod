#!/usr/bin/env python3
"""Validate a BAV3 container's header, packet boundaries and XOR/RLE stream."""
import argparse
import struct
from pathlib import Path
from convert_bav3 import HEADER, GRAY_BYTES, RGB_BYTES, decode_delta

def inspect(path:Path,validate=True):
    with path.open('rb') as f:
        head=f.read(HEADER.size)
        if len(head)!=HEADER.size:raise ValueError('Truncated BAV3 header')
        magic,w,h,fps,n,off,size,audio,fmt,flags=HEADER.unpack(head)
        if magic!=b'BAV3' or (w,h)!=(160,128) or fmt not in (1,2) or not (1<=fps<=120):
            raise ValueError('Invalid/unsupported header')
        length=path.stat().st_size
        if off<HEADER.size or off+size>length or (audio==0 and size!=0):
            raise ValueError('Invalid audio window')
        fb=GRAY_BYTES if fmt==1 else RGB_BYTES
        pixels=bytes(fb)
        for i in range(n):
            if f.tell()+2>off:raise ValueError(f'Missing packet {i}')
            sz=struct.unpack('<H',f.read(2))[0]
            if not sz or f.tell()+sz>off:raise ValueError(f'Packet {i} exceeds video end')
            packet=f.read(sz)
            if validate:pixels=decode_delta(pixels,packet)
        if f.tell()!=off:raise ValueError('Frame count/offset mismatch')
        print(f'{path.name}: VALID BAV3 | {w}x{h} | {fps} FPS | {n} frames | '+
              f'{"GRAY6" if fmt==1 else "RGB565"} | audio={size:,} B | total={length:,} B')
        return n

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('path',type=Path)
    ap.add_argument('--fast',action='store_true',help='validate bounds without decoding every delta')
    args=ap.parse_args();inspect(args.path,not args.fast)
