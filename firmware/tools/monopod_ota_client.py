#!/usr/bin/env python3
"""MonoPod OTA v1 reference client. Python stdlib only; manual local-network tool.

Usage:
  python tools/monopod_ota_client.py info --url http://192.168.4.1
  python tools/monopod_ota_client.py upload --url http://192.168.4.1 \
      --file DIY_iPod_Mono.ino.bin --pin 123456

Never use this code over an untrusted or internet-exposed HTTP network.
"""
from __future__ import annotations
import argparse
import hashlib
import http.client
import json
from pathlib import Path
import sys
import uuid
from urllib.parse import urlsplit
from urllib.request import urlopen


def ota_info(url: str) -> dict:
    with urlopen(url.rstrip('/') + '/api/ota/info', timeout=8) as response:
        return json.load(response)


def post_firmware(url: str, firmware: Path, pin: str) -> tuple[int, str]:
    target = urlsplit(url)
    if target.scheme != 'http' or not target.hostname or target.path not in ('', '/'):
        raise ValueError('Expected plain local HTTP base URL, such as http://192.168.4.1')
    if len(pin) != 6 or not pin.isascii() or not pin.isdecimal():
        raise ValueError('PIN must contain exactly 6 decimal digits from MonoPod screen')
    if not firmware.is_file():
        raise FileNotFoundError(firmware)
    size = firmware.stat().st_size
    digest = hashlib.sha256()
    with firmware.open('rb') as stream:
        if stream.read(1) != b'\xe9':
            raise ValueError('Not an ESP application .bin (first byte must be 0xE9)')
        stream.seek(0)
        for data in iter(lambda: stream.read(64 * 1024), b''):
            digest.update(data)
    info = ota_info(url)
    if not info.get('supported'):
        raise RuntimeError('Device reports unsupported OTA partition layout: USB reflash required')
    if size < 1024 or size > int(info.get('max_image_bytes', 0)):
        raise ValueError(f'File size {size} does not fit OTA slot ({info.get("max_image_bytes")} bytes)')
    boundary = 'mpodota-' + uuid.uuid4().hex
    prefix = (f'--{boundary}\r\nContent-Disposition: form-data; name="firmware"; '
              f'filename="firmware.bin"\r\nContent-Type: application/octet-stream\r\n\r\n').encode('ascii')
    suffix = (f'\r\n--{boundary}--\r\n').encode('ascii')
    content_length = len(prefix) + size + len(suffix)
    conn = http.client.HTTPConnection(target.hostname, target.port or 80, timeout=90)
    try:
        conn.putrequest('POST', '/api/ota/upload')
        conn.putheader('Content-Type', f'multipart/form-data; boundary={boundary}')
        conn.putheader('Content-Length', str(content_length))
        conn.putheader('X-MonoPod-OTA-PIN', pin)
        conn.putheader('X-MonoPod-OTA-Size', str(size))
        conn.putheader('X-MonoPod-OTA-SHA256', digest.hexdigest())
        conn.endheaders()
        conn.send(prefix)
        sent, last_percent = 0, -1
        with firmware.open('rb') as stream:
            for block in iter(lambda: stream.read(8192), b''):
                conn.send(block)
                sent += len(block)
                percent = sent * 100 // size
                if percent // 5 != last_percent // 5:
                    print(f'Uploading: {percent}%', file=sys.stderr)
                    last_percent = percent
        conn.send(suffix)
        result = conn.getresponse()
        body = result.read().decode('utf-8', errors='replace')
        return result.status, body
    finally:
        conn.close()


def main() -> int:
    parser = argparse.ArgumentParser(description='MonoPod Wi-Fi OTA (local network only)')
    sub = parser.add_subparsers(dest='action', required=True)
    for action in ('info', 'upload'):
        p = sub.add_parser(action)
        p.add_argument('--url', required=True, help='Wi-Fi Sync address, e.g. http://192.168.4.1')
        if action == 'upload':
            p.add_argument('--file', required=True, type=Path, help='Arduino sketch .bin, not merged/bootloader image')
            p.add_argument('--pin', required=True, help='6 digits displayed on MonoPod')
    args = parser.parse_args()
    try:
        if args.action == 'info':
            print(json.dumps(ota_info(args.url), indent=2, ensure_ascii=False))
            return 0
        status, body = post_firmware(args.url, args.file, args.pin)
        print(f'HTTP {status}: {body}')
        if status == 200:
            print('Image verified, device reboot scheduled. Re-enter Wi-Fi Sync to verify running firmware.')
            return 0
        return 1
    except Exception as ex:
        print(f'OTA error: {ex}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
