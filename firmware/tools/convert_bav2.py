#!/usr/bin/env python3
"""Convert user-owned Bad Apple MP4 to MonoPod BAV2 (160x128 2bpp delta/RLE + appended MP3).
Compatible BAV1 files continue to play without sound in the new firmware.
MP3 audio uses 44,100 Hz mono, 64 kbit/s by default. I2S decoding outputs PCM at 16-bit.
"""
from pathlib import Path
import argparse
import subprocess
import struct
import tempfile
import os

W,H=160,128
N=W*H//4

def pack_grey(frame:bytes)->bytes:
    if len(frame)!=W*H: raise ValueError('truncated frame')
    out=bytearray(N)
    for i in range(N):
        j=i*4
        a,b,c,d=(min(3,(frame[j+k]+42)//85) for k in range(4))
        out[i]=(a<<6)|(b<<4)|(c<<2)|d
    return bytes(out)

def encode_delta(prev:bytes,cur:bytes)->bytes:
    delta=bytes(a^b for a,b in zip(prev,cur))
    out=bytearray();i=0
    while i<len(delta):
        j=i
        if delta[i]==0:
            while j<len(delta) and delta[j]==0 and j-i<128:j+=1
            out.append(0x80|(j-i-1))
        else:
            while j<len(delta) and delta[j]!=0 and j-i<128:j+=1
            out.append(j-i-1);out.extend(delta[i:j])
        i=j
    return bytes(out)

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('input',type=Path)
    ap.add_argument('output',type=Path)
    ap.add_argument('--fps',type=int,default=15)
    ap.add_argument('--mp3-bitrate',default='64k')
    opt=ap.parse_args()
    if not 1<=opt.fps<=25:ap.error('fps must be 1..25')
    opt.output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory() as td:
        mp3=Path(td)/'sound.mp3'
        subprocess.run(['ffmpeg','-y','-hide_banner','-loglevel','error','-i',str(opt.input),
            '-vn','-map','0:a:0','-ar','44100','-ac','1','-c:a','libmp3lame','-b:a',opt.mp3_bitrate,
            '-write_xing','1','-id3v2_version','0',str(mp3)],check=True)
        cmd=['ffmpeg','-hide_banner','-loglevel','error','-i',str(opt.input),'-an',
             '-vf',f'fps={opt.fps},scale=160:128:force_original_aspect_ratio=decrease:flags=area,'
                   'pad=160:128:(ow-iw)/2:(oh-ih)/2,format=gray',
             '-pix_fmt','gray','-f','rawvideo','pipe:1']
        process=subprocess.Popen(cmd,stdout=subprocess.PIPE)
        prev=bytes(N);frames=0
        with opt.output.open('wb') as output:
            output.write(b'BAV2'+struct.pack('<HHBIIIB',W,H,opt.fps,0,0,0,1))
            while True:
                raw=process.stdout.read(W*H)
                if not raw: break
                cur=pack_grey(raw)
                delta=encode_delta(prev,cur)
                if len(delta)>65535: raise RuntimeError('encoded frame exceeds 64KiB')
                output.write(struct.pack('<H',len(delta)));output.write(delta)
                prev=cur;frames+=1
            if process.wait()!=0: raise RuntimeError('FFmpeg video decode failed')
            audio_offset=output.tell()
            audio_size=mp3.stat().st_size
            with mp3.open('rb') as audio:
                while chunk:=audio.read(65536):output.write(chunk)
            output.seek(9)
            output.write(struct.pack('<IIIB',frames,audio_offset,audio_size,1))
        print(f'BAV2 {W}x{H} {opt.fps} fps {frames} frames, video={audio_offset:,} B, MP3={audio_size:,} B, total={opt.output.stat().st_size:,} B')
        print('Copy to SD:/MonoPod/badapple.bav; no Flash embedding needed')
if __name__=='__main__': main()
