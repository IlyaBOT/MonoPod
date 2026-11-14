"""MonoPod USB Sync v2 over the native ESP32-S3 USB CDC port.

NOT Apple iAP/iTunes. Streaming, CRC32-checked, device-side temporary file and
back-pressure: the device requests each chunk before receiving it.
"""
from __future__ import annotations

import argparse
import time
import zlib
from pathlib import Path

SUPPORTED = {'.mp3', '.wav', '.flac', '.aac'}
MAX_FILE_SIZE = 1024 * 1024 * 1024


def validate_name(path: Path) -> str:
    name = path.name
    if not 5 <= len(name.encode('utf-8')) <= 90 or name.startswith('.') or '..' in name:
        raise ValueError(f'Invalid filename: {name!r}')
    if path.suffix.lower() not in SUPPORTED:
        raise ValueError(f'Unsupported format: {path.suffix}')
    if any(ord(ch) < 32 or ch in '/\\:<>"|*?' for ch in name):
        raise ValueError('Filename contains forbidden characters')
    return name


def file_crc_size(path: Path) -> tuple[int, int]:
    size = path.stat().st_size
    if not 0 < size <= MAX_FILE_SIZE:
        raise ValueError(f'File must be 1..{MAX_FILE_SIZE} bytes')
    checksum = 0
    with path.open('rb') as fp:
        for data in iter(lambda: fp.read(64 * 1024), b''):
            checksum = zlib.crc32(data, checksum)
    return size, checksum


class UsbClient:
    def __init__(self, port: str, transport=None):
        self.port = port
        self.serial = transport
        self.owned = transport is None
        self._sync_active = False
        self._streaming = False

    def __enter__(self):
        if self.serial is None:
            try:
                import serial
            except ImportError as exc:
                raise RuntimeError('Install pyserial: python -m pip install pyserial') from exc
            self.serial = serial.Serial(self.port, 115200, timeout=2, write_timeout=8)
        # Some USB CDC terminals reset/reenumerate the port on connection.
        deadline = time.monotonic() + 12
        while True:
            try:
                self.send('HELLO')
                message = self.response()
                if message.startswith('READY 1 ') or message.startswith('READY 2 ') or message.startswith('READY 3 '):
                    return self
            except (TimeoutError, OSError):
                pass
            if time.monotonic() >= deadline:
                self.close()
                raise TimeoutError('MonoPod not responding on USB CDC port (check USB port / CDC On Boot)')
            time.sleep(0.4)

    def __exit__(self, *_):
        self.close()

    def close(self):
        try:
            # Never append protocol text to a half-written binary packet.
            if self.serial is not None and self._sync_active and not self._streaming:
                self.end_sync()
        finally:
            if self.owned and self.serial is not None:
                self.serial.close()
                self.serial = None

    def start_sync(self):
        if self._sync_active:
            return
        self.send('SYNC START')
        if self.response() != 'OK SYNC_STARTED':
            raise RuntimeError('Failed to enter USB Sync mode')
        self._sync_active = True

    def end_sync(self):
        if not self._sync_active:
            return
        if self._streaming:
            raise RuntimeError('Cannot end USB Sync during a transfer')
        self.send('SYNC END')
        if self.response() != 'OK SYNC_ENDED':
            raise RuntimeError('Failed to leave USB Sync mode')
        self._sync_active = False

    def send(self, payload: str):
        self.serial.write(('MPOD1 ' + payload + '\n').encode('utf-8'))

    def response(self) -> str:
        for _ in range(6):
            line = self.serial.readline().decode('utf-8', 'replace').strip()
            if line.startswith('MPOD1 '):
                payload = line[6:]
                if payload.startswith('ERR '):
                    raise RuntimeError(f'Device: {payload}')
                return payload
        raise TimeoutError('Timed out waiting for MonoPod response')

    def info(self) -> str:
        self.send('INFO')
        answer = self.response()
        if not answer.startswith('OK INFO '):
            raise RuntimeError('Unexpected response: ' + answer)
        return answer

    def upload(self, path: str | Path, progress=None, replace: bool = False):
        path = Path(path)
        name = validate_name(path)
        size, checksum = file_crc_size(path)
        self.start_sync()
        self.send(f'{"REPLACE" if replace else "PUT"} {size} {checksum:08X} {name}')
        sent = 0
        with path.open('rb') as fp:
            while sent < size:
                reply = self.response()
                if not reply.startswith('NEXT '):
                    raise RuntimeError(f'Expected NEXT, got {reply}')
                n = int(reply[5:])
                self._streaming = True
                if not 0 < n <= 1024 or n > size - sent:
                    raise RuntimeError(f'Invalid chunk request {n}')
                data = fp.read(n)
                if len(data) != n:
                    raise IOError('Unexpected EOF while uploading')
                written = self.serial.write(data)
                if written != len(data):
                    raise IOError('Partial USB CDC write')
                sent += n
                if progress:
                    progress(sent, size)
        answer = self.response()
        self._streaming = False
        if answer not in ('OK SAVED', 'OK REPLACED'):
            raise RuntimeError('Unexpected transfer result: ' + answer)
        return f'{name}: {sent} bytes, CRC32 {checksum:08X} ({answer})'

    def delete(self, name: str) -> str:
        # Only the filename is accepted; never let this command act on arbitrary SD paths.
        checked = validate_name(Path(name))
        if checked != name:
            raise ValueError('DELETE requires a basename')
        self.start_sync()
        self.send('DELETE ' + checked)
        answer = self.response()
        if answer != 'OK DELETED':
            raise RuntimeError('Unexpected delete response: ' + answer)
        return f'{checked}: deleted'

    def scan(self) -> str:
        self.start_sync()
        self.send('SCAN')
        result = self.response()
        if not result.startswith('OK SCANNED '):
            raise RuntimeError('Unexpected scan response: ' + result)
        return result


def get_ports():
    try:
        from serial.tools import list_ports
    except ImportError as exc:
        raise RuntimeError('Install pyserial: python -m pip install pyserial') from exc
    return [(p.device, p.description) for p in list_ports.comports()]


def main(argv=None):
    parser = argparse.ArgumentParser(description='MonoPod ESP32-S3 USB CDC music sync')
    parser.add_argument('--port', help='Native USB CDC serial port, e.g. COM7 or /dev/ttyACM0')
    parser.add_argument('action', choices=['ports', 'info', 'send', 'delete'])
    parser.add_argument('--replace', action='store_true', help='Replace existing same-name track atomically')
    parser.add_argument('files', nargs='*')
    args = parser.parse_args(argv)
    if args.action == 'ports':
        for port, label in get_ports():
            print(port, label)
        return
    if not args.port:
        parser.error('--port is required unless action is ports')
    with UsbClient(args.port) as device:
        print(device.info())
        if args.action == 'send':
            if not args.files:
                parser.error('send requires one or more audio files')
            for fn in args.files:
                print(device.upload(fn, replace=args.replace))
            print(device.scan())
        elif args.action == 'delete':
            if not args.files:
                parser.error('delete requires one or more filenames')
            for name in args.files:
                print(device.delete(name))
            print(device.scan())


if __name__ == '__main__':
    main()
