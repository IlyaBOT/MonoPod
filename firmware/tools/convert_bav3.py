#!/usr/bin/env python3
"""MonoPod BAV3: 160x128 GRAY6 (64 levels) or RGB565, variable 1..120 FPS,
optional appended MP3 audio. Delta frames encoded with bytewise XOR + zero-skip RLE.
Old BAV1/BAV2 decoding remains in the device; BAV3 needs new firmware.
"""
import argparse
import struct
import subprocess
import tempfile
from pathlib import Path

WIDTH = 160
HEIGHT = 128
GRAY_BYTES = WIDTH * HEIGHT * 6 // 8
RGB_BYTES = WIDTH * HEIGHT * 2
GRAY_LUT = bytes((v * 63 + 127) // 255 for v in range(256))
HEADER = struct.Struct('<4sHHHIIIBBB') # 25 bytes
MODES = {'gray6': 1, 'rgb565': 2}


def pack_gray6(data: bytes) -> bytes:
    if len(data) != WIDTH * HEIGHT:
        raise ValueError('Wrong source gray frame size')
    result = bytearray(GRAY_BYTES)
    dest = 0
    for i in range(0, len(data), 4):
        a, b, c, d = (GRAY_LUT[data[i+j]] for j in range(4))
        result[dest] = (a << 2) | (b >> 4)
        result[dest+1] = ((b & 15) << 4) | (c >> 2)
        result[dest+2] = ((c & 3) << 6) | d
        dest += 3
    return bytes(result)


def unpack_gray6(data: bytes) -> bytes:
    if len(data) != GRAY_BYTES:
        raise ValueError('Wrong packed gray frame size')
    out = bytearray(WIDTH * HEIGHT)
    for src in range(0, len(data), 3):
        a,b,c = data[src:src+3]
        dst = (src//3)*4
        out[dst:dst+4] = ((a>>2), ((a&3)<<4)|(b>>4), ((b&15)<<2)|(c>>6), (c&63))
    return bytes(out)


def encode_delta(previous: bytes, current: bytes) -> bytes:
    if len(previous) != len(current):
        raise ValueError('Frame lengths differ')
    delta = bytes(a ^ b for a,b in zip(previous, current))
    output = bytearray()
    i = 0
    while i < len(delta):
        start = i
        if delta[i] == 0:
            while i < len(delta) and delta[i] == 0 and i - start < 128:
                i += 1
            output.append(0x80 | (i-start-1))
        else:
            while i < len(delta) and delta[i] != 0 and i - start < 128:
                i += 1
            output.append(i-start-1)
            output.extend(delta[start:i])
    return bytes(output)


def decode_delta(previous: bytes, encoded: bytes) -> bytes:
    result=bytearray(previous)
    pos=used=0
    while used<len(encoded):
        control=encoded[used];used+=1
        count=(control&127)+1
        if pos+count>len(result):
            raise ValueError('Delta writes past frame buffer')
        if control&128:
            pos+=count
        else:
            if used+count>len(encoded):
                raise ValueError('Truncated delta literal')
            for j in range(count):
                result[pos+j]^=encoded[used+j]
            pos+=count;used+=count
    if pos!=len(result):
        raise ValueError('Delta frame was not fully described')
    return bytes(result)


def convert(source: Path, destination: Path, *, fps: int=20, pixel_format='gray6',
            bitrate='64k', audio=True, seconds=None):
    if not 1 <= fps <= 120:
        raise ValueError('FPS must be 1..120')
    if pixel_format not in MODES:
        raise ValueError('Unknown pixel format')
    destination.parent.mkdir(parents=True, exist_ok=True)
    frame_size=GRAY_BYTES if pixel_format=='gray6' else RGB_BYTES
    with tempfile.TemporaryDirectory() as td:
        mp3=Path(td)/'audio.mp3'
        if audio:
            cmd=['ffmpeg','-y','-hide_banner','-loglevel','error','-i',str(source)]
            if seconds is not None:cmd+=['-t',str(seconds)]
            cmd+=['-map','0:a:0','-vn','-ar','44100','-ac','1',
                  '-c:a','libmp3lame','-b:a',bitrate,'-write_xing','1',
                  '-id3v2_version','0',str(mp3)]
            subprocess.run(cmd,check=True)
        vf=f'fps={fps},scale=160:128:force_original_aspect_ratio=decrease:flags=area,' \
           'pad=160:128:(ow-iw)/2:(oh-ih)/2,' \
           f'format={"gray" if pixel_format=="gray6" else "rgb565le"}'
        fmt='gray' if pixel_format=='gray6' else 'rgb565le'
        cmd=['ffmpeg','-hide_banner','-loglevel','error','-i',str(source)]
        if seconds is not None:cmd+=['-t',str(seconds)]
        cmd+=['-an','-vf',vf,'-pix_fmt',fmt,'-f','rawvideo','pipe:1']
        frame_raw_size=WIDTH*HEIGHT*(1 if pixel_format=='gray6' else 2)
        process=subprocess.Popen(cmd,stdout=subprocess.PIPE)
        frames=0
        previous=bytes(frame_size)
        try:
            with destination.open('wb') as out:
                out.write(HEADER.pack(b'BAV3',WIDTH,HEIGHT,fps,0,0,0,1 if audio else 0,MODES[pixel_format],0))
                while True:
                    raw=process.stdout.read(frame_raw_size)
                    if not raw:break
                    if len(raw)!=frame_raw_size:raise RuntimeError('Partial raw video frame')
                    current=pack_gray6(raw) if pixel_format=='gray6' else raw
                    encoded=encode_delta(previous,current)
                    if not encoded or len(encoded)>65535:
                        raise RuntimeError('Delta packet too large')
                    out.write(struct.pack('<H',len(encoded)))
                    out.write(encoded)
                    previous=current;frames+=1
                process.stdout.close()
                if process.wait()!=0:raise RuntimeError('FFmpeg video conversion failed')
                video_end=out.tell()
                size=mp3.stat().st_size if audio else 0
                if audio:
                    with mp3.open('rb') as snd:
                        while block:=snd.read(65536):out.write(block)
                out.seek(0)
                out.write(HEADER.pack(b'BAV3',WIDTH,HEIGHT,fps,frames,video_end,size,
                                      1 if audio else 0, MODES[pixel_format],0))
        finally:
            if process.poll() is None:
                process.kill();process.wait()
    print(f'BAV3 / {pixel_format} / {fps} FPS / {frames} frames')
    print(f'Video: {video_end:,} bytes; MP3 audio: {size:,} bytes; total: {destination.stat().st_size:,} bytes')
    print('Install on microSD: /MonoPod/badapple.bav')


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('input',type=Path)
    ap.add_argument('output',type=Path)
    ap.add_argument('--fps',type=int,default=20,help='1-120; 20 recommended for Bad Apple')
    ap.add_argument('--pixel-format',choices=MODES,default='gray6')
    ap.add_argument('--mp3-bitrate',default='64k')
    ap.add_argument('--no-audio',action='store_true')
    ap.add_argument('--seconds',type=float,help='convert only first N seconds (debugging)')
    args=ap.parse_args()
    convert(args.input,args.output,fps=args.fps,pixel_format=args.pixel_format,
            bitrate=args.mp3_bitrate,audio=not args.no_audio,seconds=args.seconds)

if __name__=='__main__':main()
