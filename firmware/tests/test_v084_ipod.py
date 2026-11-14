from pathlib import Path
import tempfile
import pytest
from Studio.usb_sync import UsbClient,validate_name
from tools.itunesdb_inspect import parse_database,BadDatabase


def rec(tag:bytes, head:bytes=b'', children:bytes=b''):
    # Bounded generic little-endian iTunesDB-style record.
    hl=12+len(head)
    return tag+hl.to_bytes(4,'little')+(hl+len(children)).to_bytes(4,'little')+head+children


class FakeVersion2:
    def __init__(self):
        self.responses=[]
        self.tracks={'song.mp3':b'old'}
        self.remaining=0
        self.action=''
        self.name=''
        self.data=bytearray()
        self.in_sync=False
    def write(self,data):
        if self.remaining:
            self.data.extend(data)
            self.remaining-=len(data)
            if self.remaining:
                self.responses.append(f'MPOD1 NEXT {self.remaining}\n'.encode())
            else:
                existed=self.name in self.tracks
                self.tracks[self.name]=bytes(self.data)
                self.responses.append((b'MPOD1 OK REPLACED\n' if existed else b'MPOD1 OK SAVED\n'))
            return len(data)
        cmd=data.decode().rstrip('\n')
        if cmd=='MPOD1 HELLO':self.responses.append(b'MPOD1 READY 2 SD=1\n')
        elif cmd=='MPOD1 SYNC START':
            self.in_sync=True;self.responses.append(b'MPOD1 OK SYNC_STARTED\n')
        elif cmd=='MPOD1 SYNC END':
            assert self.in_sync and self.remaining==0
            self.in_sync=False;self.responses.append(b'MPOD1 OK SYNC_ENDED\n')
        elif cmd=='MPOD1 INFO':self.responses.append(b'MPOD1 OK INFO SD=1 TRACKS=1 TOTAL_BYTES=4294967296 FREE_BYTES=4294963200 GUID=4D50AABBCCDDEEFF\n')
        elif cmd=='MPOD1 SCAN':
            assert self.in_sync
            self.responses.append(f'MPOD1 OK SCANNED {len(self.tracks)}\n'.encode())
        elif cmd.startswith('MPOD1 DELETE '):
            assert self.in_sync
            name=cmd[13:]
            self.tracks.pop(name)
            self.responses.append(b'MPOD1 OK DELETED\n')
        elif cmd.startswith(('MPOD1 PUT ','MPOD1 REPLACE ')):
            assert self.in_sync
            parts=cmd.split(' ',4)
            self.action=parts[1]
            self.remaining=int(parts[2]);self.name=parts[4];self.data=bytearray()
            self.responses.append(f'MPOD1 NEXT {self.remaining}\n'.encode())
        else:raise AssertionError(cmd)
        return len(data)
    def readline(self):return self.responses.pop(0) if self.responses else b''


def test_sync_v2_replace_delete_and_capacity():
    fake=FakeVersion2()
    with tempfile.TemporaryDirectory() as d:
        p=Path(d)/'song.mp3';p.write_bytes(b'new metadata contents')
        with UsbClient('FAKE',transport=fake) as c:
            assert 'TOTAL_BYTES=4294967296' in c.info()
            assert 'GUID=4D50' in c.info()
            assert 'REPLACED' in c.upload(p,replace=True)
            assert c.scan()=='OK SCANNED 1'
            assert c.delete('song.mp3')=='song.mp3: deleted'
            assert c.scan()=='OK SCANNED 0'
    assert not fake.tracks
    assert not fake.in_sync


def test_delete_rejects_paths():
    with UsbClient('FAKE',transport=FakeVersion2()) as c:
        with pytest.raises(ValueError):c.delete('../song.mp3')
        with pytest.raises(ValueError):c.delete('folder/song.mp3')


def test_itunesdb_inspector_accepts_minimal_empty_db():
    mhlt=rec(b'mhlt',(0).to_bytes(4,'little'))
    mhsd=rec(b'mhsd',(1).to_bytes(4,'little'),mhlt)
    mhbd=rec(b'mhbd',b'\0'*4,mhsd)
    result=parse_database(mhbd)
    assert result['track_count']==0
    assert result['datasets']==[1]


def test_itunesdb_inspector_rejects_bad_lengths():
    with pytest.raises(BadDatabase):parse_database(b'mhbd'+b'\0'*12)
