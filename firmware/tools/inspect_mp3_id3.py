#!/usr/bin/env python3
"""Inspect MP3 ID3 tag frames without loading artwork. Python 3 stdlib."""
import pathlib
import sys


def syn(b):
    if len(b) != 4 or any(byte & 0x80 for byte in b):
        raise ValueError('invalid syncsafe length')
    return (b[0] << 21) | (b[1] << 14) | (b[2] << 7) | b[3]


def decode(raw):
    if not raw:
        return ''
    enc, value = raw[0], raw[1:]
    codecs = {0:'latin-1',1:'utf-16',2:'utf-16-be',3:'utf-8'}
    if enc not in codecs:
        return f'<unsupported encoding {enc}>'
    try:
        return value.decode(codecs[enc], errors='replace').split('\x00', 1)[0].strip()
    except UnicodeError as ex:
        return f'<decode error: {ex}>'


def inspect(path):
    path = pathlib.Path(path)
    with path.open('rb') as file:
        head = file.read(10)
        if head[:3] != b'ID3':
            print('No ID3v2 at offset 0; check ID3v1 or external metadata.'); return
        ver, flags, size = head[3], head[5], syn(head[6:10])
        print(f'ID3v2.{ver}, flags=0x{flags:02x}, tag_size={size:,} bytes')
        end, offset = min(path.stat().st_size, size + 10), 10
        if flags & 0x40 and ver in (3,4):
            file.seek(offset)
            ext = file.read(4)
            extent = int.from_bytes(ext, 'big') + 4 if ver == 3 else syn(ext)
            print(f'Extended header: {extent:,} bytes')
            offset += extent
        names = {'TIT2':'Title', 'TPE1':'Artist', 'TALB':'Album', 'TCON':'Genre',
                 'TT2':'Title', 'TP1':'Artist', 'TAL':'Album'}
        found = set()
        for index in range(1024):
            head_size = 6 if ver == 2 else 10
            if offset + head_size > end:
                break
            file.seek(offset)
            fh = file.read(head_size)
            if len(fh) < head_size or not fh[0]:
                break
            fid = fh[:3 if ver == 2 else 4].decode('latin-1')
            n = (int.from_bytes(fh[3:6], 'big') if ver == 2 else
                 int.from_bytes(fh[4:8], 'big') if ver == 3 else syn(fh[4:8]))
            data_offset = offset + head_size
            if not n or n > end - data_offset:
                print(f'Invalid frame {fid} at {offset:,}: size={n:,}'); break
            comment = ''
            if fid in names:
                comment = f' ({names[fid]})'
                if n <= 4096:
                    file.seek(data_offset)
                    payload = file.read(n)
                    if ver == 4 and fh[9] & 0x01:
                        payload = payload[4:]
                    if (flags & 0x80) or (ver == 4 and fh[9] & 0x02):
                        payload = payload.replace(b'\xff\x00', b'\xff')
                    comment += ' = ' + repr(decode(payload))
                    found.add(names[fid])
                else:
                    comment += ' [text frame too large for firmware]'
            print(f'{fid:<5} offset={offset:>9,} size={n:>9,}{comment}')
            offset = data_offset + n
        for key in ('Title', 'Artist', 'Album'):
            if key not in found:
                print(f'WARNING: {key} was not decoded from ID3v2.')
        print('This tool does not modify the MP3.')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit('Usage: python3 inspect_mp3_id3.py "/path/to/song.mp3"')
    inspect(sys.argv[1])
