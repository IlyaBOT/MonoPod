#!/usr/bin/env python3
"""Encode a user-supplied MP4 into MonoPod BAV1 160x128, 2-bit grayscale, delta+RLE.
  python convert_badapple.py input.mp4 output.bav --fps 15
Audio is intentionally omitted (TFT playback benchmark); no third-party content bundled.
"""
import argparse, subprocess, struct, os, sys
W,H=160,128
N=W*H//4

def pack_grey(frame):
    if len(frame)!=W*H:raise ValueError('invalid raw frame length')
    out=bytearray(N)
    for i in range(0,len(frame),4):
        a,b,c,d=(min(3,(frame[i+j]+42)//85) for j in range(4))
        out[i//4]=(a<<6)|(b<<4)|(c<<2)|d
    return bytes(out)

def encode_delta(prev,cur):
    delta=bytes(a^b for a,b in zip(prev,cur));out=bytearray();i=0
    while i<len(delta):
        if delta[i]==0:
            j=i
            while j<len(delta) and delta[j]==0 and j-i<128:j+=1
            out.append(0x80|(j-i-1));i=j
        else:
            j=i
            while j<len(delta) and delta[j]!=0 and j-i<128:j+=1
            out.append(j-i-1);out.extend(delta[i:j]);i=j
    return bytes(out)

def main():
    pa=argparse.ArgumentParser(description=__doc__)
    pa.add_argument('input');pa.add_argument('output');pa.add_argument('--fps',type=int,default=15)
    pa.add_argument('--embed-header',help='Optional PROGMEM header: DIY_iPod_Mono/BadAppleEmbedded.h (large, needs APP partition)')
    opt=pa.parse_args()
    if not 1<=opt.fps<=25:pa.error('fps must be 1..25')
    cmd=['ffmpeg','-hide_banner','-loglevel','error','-i',opt.input,'-an',
         '-vf',f'fps={opt.fps},scale=160:128:flags=area,format=gray',
         '-pix_fmt','gray','-f','rawvideo','pipe:1']
    proc=subprocess.Popen(cmd,stdout=subprocess.PIPE)
    prev=bytes(N);frames=0
    with open(opt.output,'wb') as f:
        f.write(b'BAV1'+struct.pack('<HHBI',W,H,opt.fps,0))
        while True:
            data=proc.stdout.read(W*H)
            if not data:break
            if len(data)!=W*H:raise RuntimeError('incomplete ffmpeg frame')
            cur=pack_grey(data)
            chunk=encode_delta(prev,cur)
            if len(chunk)>65535:raise RuntimeError('frame too large')
            f.write(struct.pack('<H',len(chunk))+chunk)
            prev=cur;frames+=1
        f.seek(9);f.write(struct.pack('<I',frames))
    if proc.wait()!=0:raise RuntimeError('ffmpeg failed')
    print(f'BAV1: {W}x{H} 4 grayscale levels, {opt.fps} fps, frames={frames}, file={os.path.getsize(opt.output):,} bytes')
    print('Copy output to microSD:/MonoPod/badapple.bav')
    if opt.embed_header:
        binary=open(opt.output,'rb').read()
        with open(opt.embed_header,'w',encoding='utf-8') as h:
            h.write('#pragma once\n#include <Arduino.h>\n')
            h.write('static const uint8_t BadAppleEmbedded[] PROGMEM={\n')
            for off in range(0,len(binary),24):
                h.write('  '+','.join('0x%02x'%b for b in binary[off:off+24])+',\n')
            h.write('};\nstatic constexpr uint32_t BadAppleEmbeddedSize=sizeof(BadAppleEmbedded);\n')
        print(f'Embedded header: {opt.embed_header}, compressed video {len(binary):,} bytes')
if __name__=='__main__':main()
