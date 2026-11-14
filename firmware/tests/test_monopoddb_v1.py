from pathlib import Path
import sys
import zlib
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from MonoPodDB_Desktop_Reference_v1 import parse_db, MonoPodError


def _record(title='Это 100% тест'):
    values=['T','/Music/Тест%25.flac','12345','0','12345678','00000000','240000','2026','4',
            '44100','411','FLAC','.flac',title.replace('%','%25'),'Тест','Album','Album Artist','Pop','10584000','44100']
    return ('\t'.join(values)+'\n').encode()


def _db(records):
    raw=b''.join(records)
    return f'MPODDB\t1\t4\t{len(records)}\t0\n'.encode()+raw+f'END\t{len(records)}\t{zlib.crc32(raw):08X}\n'.encode()


def test_parse_unicode_fields_and_invalid_date():
    gen,tracks=parse_db(_db([_record()]))
    assert gen==4 and len(tracks)==1
    assert tracks[0].title=='Это 100% тест'
    assert tracks[0].modified_unix is None
    assert tracks[0].album_artist=='Album Artist'
    assert tracks[0].duration_ms==240000
    assert tracks[0].path=='/Music/Тест%.flac'


def test_corrupt_payload_rejected():
    data=bytearray(_db([_record()]))
    data[55]^=1
    try:parse_db(bytes(data))
    except MonoPodError:return
    assert False,'CRC mismatch accepted'


def test_truncated_rejected():
    try:parse_db(_db([_record()])[:-2])
    except MonoPodError:return
    assert False,'Truncated footer accepted'
