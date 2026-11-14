#!/usr/bin/env python3
"""Read-only, bounds-checked inspection of classic iPod iTunesDB containers.

Not a DB writer and not a substitute for libgpod. Designed for comparing
real iPod 3G/4G iTunesDB captures with a future MonoPod decoder.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

MAX_DB_BYTES = 128 * 1024 * 1024


class BadDatabase(ValueError):
    pass


def u32(buf: bytes, off: int) -> int:
    if off < 0 or off + 4 > len(buf):
        raise BadDatabase(f'Unexpected EOF at 0x{off:x}')
    return int.from_bytes(buf[off:off + 4], 'little')


def node(buf: bytes, offset: int, limit: int, expected: bytes | None = None):
    if offset + 12 > limit:
        raise BadDatabase(f'Incomplete header at 0x{offset:x}')
    tag = buf[offset:offset + 4]
    if expected and tag != expected:
        raise BadDatabase(f'Expected {expected!r} at 0x{offset:x}; got {tag!r}')
    header = u32(buf, offset + 4)
    size = u32(buf, offset + 8)
    if header < 12 or size < header or offset + size > limit:
        raise BadDatabase(f'Invalid {tag!r} lengths at 0x{offset:x}')
    return tag, offset + header, offset + size


def parse_database(data: bytes) -> dict:
    if len(data) > MAX_DB_BYTES:
        raise BadDatabase('File too large for this inspector')
    tag, pos, end = node(data, 0, len(data), b'mhbd')
    result = {'bytes': len(data), 'datasets': [], 'tracks': [], 'track_count': 0}
    while pos < end:
        tag, child, next_pos = node(data, pos, end, b'mhsd')
        dataset_type = u32(data, pos + 12)
        result['datasets'].append(dataset_type)
        if dataset_type == 1:  # master track list
            _, list_start, list_end = node(data, child, next_pos, b'mhlt')
            declared = u32(data, child + 12)
            track_pos = list_start
            while track_pos < list_end:
                _, metadata_pos, track_end = node(data, track_pos, list_end, b'mhit')
                info = {'track_id': u32(data, track_pos + 16)}
                if track_pos + 0x78 <= metadata_pos:
                    info['dbid'] = data[track_pos + 0x70:track_pos + 0x78].hex()
                while metadata_pos < track_end:
                    mhod_tag, string_pos, mhod_end = node(data, metadata_pos, track_end, b'mhod')
                    mhod_type = u32(data, metadata_pos + 12)
                    if mhod_type in (1, 2, 3, 4, 5):
                        # Most old iTunesDB MHOD strings: 40-byte header, UTF-16LE data.
                        if metadata_pos + 40 <= mhod_end:
                            nbytes = u32(data, metadata_pos + 28)
                            start = metadata_pos + 40
                            if nbytes <= mhod_end - start and nbytes % 2 == 0:
                                string = data[start:start + nbytes].decode('utf-16le', 'replace')
                                info[{1: 'title', 2: 'path', 3: 'album', 4: 'artist', 5: 'genre'}[mhod_type]] = string
                    metadata_pos = mhod_end
                result['tracks'].append(info)
                track_pos = track_end
            if len(result['tracks']) != declared:
                result['warnings'] = [f'mhlt says {declared} records; parser found {len(result["tracks"])}']
        pos = next_pos
    result['track_count'] = len(result['tracks'])
    return result


def main():
    p = argparse.ArgumentParser(description='Inspect a real legacy iPod iTunesDB in read-only mode')
    p.add_argument('file', type=Path)
    p.add_argument('--limit', type=int, default=20)
    args = p.parse_args()
    report = parse_database(args.file.read_bytes())
    report['tracks'] = report['tracks'][:max(0, args.limit)]
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
