"""Offline test of wire compatibility for host YMODEM sender."""
import importlib.util
import pathlib
import tempfile
import unittest

SRC=pathlib.Path(__file__).resolve().parents[1]/"tools"/"send_audio_ymodem.py"
spec=importlib.util.spec_from_file_location("ymodem_uploader",SRC)
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class MockSerial:
    def __init__(self):
        self.pending=bytearray()
        self.written=bytearray()
        self.expected=0
        self.eot=0
        self.phase='init'
        self.size=0
        self.rx=bytearray()
        self.staged=bytearray()
    def reset_input_buffer(self): self.pending.clear()
    def write(self,raw):
        if raw.startswith(b'UPLOAD='):
            self.pending.extend(b'READY YMODEM_CRC16\nC')
            return len(raw)
        if raw==bytes([m.EOT]):
            self.eot+=1
            if self.eot==1:self.pending.append(m.NAK)
            else:self.pending.extend(bytes([m.ACK,m.CRC_REQ]));self.phase='last'
            return 1
        assert raw[0] in (m.SOH,m.STX)
        n=128 if raw[0]==m.SOH else 1024
        assert len(raw)==n+5
        assert (raw[1]^raw[2])==255
        payload=raw[3:3+n]
        assert raw[-2:]==m.crc16(payload).to_bytes(2,'big')
        if self.phase=='init':
            assert raw[1]==0
            self.size=int(payload.split(b'\0')[1].split(b'\0')[0]);self.phase='data'
            self.pending.extend(bytes([m.ACK,m.CRC_REQ]))
        elif self.phase=='last':
            assert raw[1]==0 and payload[0]==0
            self.pending.append(m.ACK);self.phase='done'
        else:
            assert raw[1]==self.expected+1 & 0xff
            self.expected=(self.expected+1)&255
            self.rx.extend(payload);self.pending.append(m.ACK)
        return len(raw)
    def read(self,n):
        if not self.pending: return b''
        b=self.pending[:n];del self.pending[:n];return bytes(b)

class TestYModem(unittest.TestCase):
    def test_crc_reference(self):self.assertEqual(m.crc16(b'123456789'),0x31C3)
    def test_tone_length(self):
        with tempfile.TemporaryDirectory() as d:
            p=pathlib.Path(d)/'t.pcm';m.make_tone(p,16000,1,1.0,896)
            self.assertEqual(p.stat().st_size,32000)
    def test_seq_wrap_after_255(self):
        raw = bytes(range(256)) * 1040  # 260 x 1024-byte packets, wraps seq to zero
        mock = MockSerial()
        m.perform_transfer(mock, raw, 16000, 1)
        self.assertEqual(mock.phase, 'done')
        self.assertEqual(mock.rx[:len(raw)], raw)

    def test_protocol_multiple_blocks_and_eot(self):
        raw=bytes(range(256))*13
        mock=MockSerial()
        m.perform_transfer(mock,raw,16000,1)
        self.assertEqual(mock.phase,'done')
        self.assertEqual(mock.eot,2)
        self.assertEqual(mock.size,len(raw))
        self.assertEqual(mock.rx[:len(raw)],raw)
    def test_1k_payload_compatibility(self):
        raw=bytes(range(256))*13
        mock=MockSerial()
        m.perform_transfer(mock,raw,16000,1,block_size=1024)
        self.assertEqual(mock.phase,'done')
        self.assertEqual(mock.rx[:len(raw)],raw)

if __name__=='__main__':unittest.main()
