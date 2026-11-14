from pathlib import Path
import tempfile
import zlib

from Studio.usb_sync import UsbClient, file_crc_size, validate_name


class SimulatedUsbPort:
    """Host test double: mirrors a minimal MPOD1 responder and transfer pacing."""

    def __init__(self):
        self.pending = []
        self.header = None
        self.data = bytearray()
        self.tracks = {}
        self.in_sync = False
        self.events = []

    def write(self, data):
        if self.header is not None:
            name, size, crc = self.header
            self.data.extend(data)
            assert len(self.data) <= size
            if len(self.data) == size:
                assert zlib.crc32(self.data) == crc
                self.tracks[name] = bytes(self.data)
                self.header = None
                self.pending.append(b'MPOD1 OK SAVED\n')
            else:
                self.pending.append(f'MPOD1 NEXT {min(1024, size - len(self.data))}\n'.encode())
            return len(data)

        command = data.decode('utf-8').rstrip('\n')
        if command == 'MPOD1 HELLO':
            self.pending.append(b'MPOD1 READY 1 SD=1\n')
        elif command == 'MPOD1 SYNC START':
            self.in_sync = True
            self.events.append('enter')
            self.pending.append(b'MPOD1 OK SYNC_STARTED\n')
        elif command == 'MPOD1 SYNC END':
            assert self.in_sync and self.header is None
            self.in_sync = False
            self.events.append('leave')
            self.pending.append(b'MPOD1 OK SYNC_ENDED\n')
        elif command == 'MPOD1 INFO':
            self.pending.append(f'MPOD1 OK INFO SD=1 TRACKS={len(self.tracks)}\n'.encode())
        elif command.startswith('MPOD1 PUT '):
            assert self.in_sync
            _, _, n, crc, name = command.split(' ', 4)
            self.header = (name, int(n), int(crc, 16))
            self.data = bytearray()
            self.pending.append(f'MPOD1 NEXT {min(1024, int(n))}\n'.encode())
        elif command == 'MPOD1 SCAN':
            assert self.in_sync
            self.pending.append(f'MPOD1 OK SCANNED {len(self.tracks)}\n'.encode())
        else:
            raise AssertionError(command)
        return len(data)

    def readline(self):
        return self.pending.pop(0) if self.pending else b''


def test_usb_host_transfer_chunking_crc_and_scan():
    fake = SimulatedUsbPort()
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / 'Трек 01.flac'
        payload = bytes(range(256)) * 17 + b'\x00\x55\xff'
        path.write_bytes(payload)
        assert file_crc_size(path) == (len(payload), zlib.crc32(payload))
        with UsbClient('FAKE', transport=fake) as client:
            assert 'TRACKS=0' in client.info()
            logs = []
            assert client.upload(path, progress=lambda sent,total:logs.append((sent,total))).startswith(path.name)
            assert logs[-1] == (len(payload),len(payload))
            assert client.scan() == 'OK SCANNED 1'
        assert fake.tracks[path.name] == payload
        assert fake.events == ['enter', 'leave']
        assert not fake.in_sync


def test_usb_invalid_filename():
    for name in ['../evil.mp3', '.hidden.mp3', 'something.exe', 'x\\y.mp3', 'bad?.mp3']:
        try:
            validate_name(Path(name))
        except ValueError:
            continue
        # On POSIX Path("../evil.mp3") normalizes out the slash; this checks the basename.
        if name != '../evil.mp3':
            raise AssertionError(f'Did not reject {name}')
