"""USB protocol probe uses real framing but does not require an ESP32."""
import importlib.util
from pathlib import Path
spec = importlib.util.spec_from_file_location('usb_probe', Path(__file__).parents[1] / 'tools' / 'usb_probe_v4.py')
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)

class FakeUSB:
    def __init__(self, responses):
        self.responses = list(responses)
        self.written = []
    def reset_input_buffer(self):
        pass
    def write(self, data):
        self.written.append(data)
        return len(data)
    def flush(self):
        pass
    def readline(self):
        return self.responses.pop(0) if self.responses else b''

def test_hello():
    f = FakeUSB([b'ESP startup\r\n', b'MPOD1 READY 4 SD=1\r\n'])
    assert probe.transact(f, 'HELLO', 0.1) == 'READY 4 SD=1'
    assert f.written == [b'MPOD1 HELLO\n']

def test_info():
    f = FakeUSB([b'MPOD1 OK {"protocol":4,"sync_mode":"none"}\n'])
    assert probe.transact(f, 'X INFO', 0.1).startswith('OK {')

def test_error():
    f = FakeUSB([b'MPOD1 ERR NO_SD\n'])
    try: probe.transact(f, 'SYNC START', .1)
    except RuntimeError as exc: assert 'NO_SD' in str(exc)
    else: raise AssertionError('expected Sync error')
