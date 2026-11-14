#!/usr/bin/env python3
"""MonoPodDB v1 client + MP3/FLAC metadata editor for MonoPod Wi-Fi Sync.

This is a standalone reference client, NOT a patch to the MonoPod Desktop GUI.
Python 3.10+. Metadata changes require: pip install mutagen

Examples:
 python MonoPodDB_Desktop_Reference_v1.py --host 192.168.1.113 list
 python MonoPodDB_Desktop_Reference_v1.py --host 192.168.1.113 refresh
 python MonoPodDB_Desktop_Reference_v1.py --host 192.168.1.113 edit --path '/Music/Track.flac' --artist 'Example' --album 'Demo'
"""
from __future__ import annotations
import argparse
import binascii
import http.client
import json
import os
from pathlib import Path
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass, asdict
from typing import Any
import zlib

MAX_DB = 8_388_608

class MonoPodError(RuntimeError):
    pass

@dataclass(frozen=True)
class Track:
    path: str
    file_size: int
    modified_unix: int | None
    fingerprint_crc32_sample: str
    full_crc32: str | None
    duration_ms: int
    year: int | None
    track_number: int | None
    sample_rate: int | None
    bitrate_kbps: int | None
    codec: str
    extension: str
    title: str
    artist: str
    album: str
    album_artist: str
    genre: str
    flac_total_samples: int
    flac_sample_rate: int


def _decode_field(s: str) -> str:
    # Same percent escaping as MonoPodDB.h: %, CR, LF, TAB and ASCII controls.
    # Do not use unquote_plus: '+' is literal in metadata.
    return urllib.parse.unquote(s, encoding='utf-8', errors='strict')


def parse_db(raw: bytes) -> tuple[int,list[Track]]:
    if len(raw) > MAX_DB:
        raise MonoPodError('Database too large')
    if not raw.endswith(b'\n'):
        raise MonoPodError('Missing database EOF delimiter')
    try:
        lines = raw.decode('utf-8', errors='strict').splitlines(keepends=True)
        header = lines[0].rstrip('\r\n').split('\t')
        if len(header) != 5 or header[0:2] != ['MPODDB','1']:
            raise MonoPodError('Unsupported MonoPodDB schema')
        generation = int(header[2]); count = int(header[3])
        if count < 0 or count > 100_000 or len(lines) != count+2:
            raise MonoPodError('Invalid count or truncated database')
        content = b''.join(line.encode('utf-8') for line in lines[1:-1]); expected=zlib.crc32(content)
        footer=lines[-1].rstrip('\r\n').split('\t')
        if len(footer) != 3 or footer[0]!='END' or int(footer[1]) != count or int(footer[2],16) != expected:
            raise MonoPodError('Database CRC32 mismatch')
        tracks=[]
        paths=set()
        for line in lines[1:-1]:
            cols=line.rstrip('\r\n').split('\t')
            if len(cols)!=20 or cols[0]!='T':
                raise MonoPodError('Invalid track record')
            path=_decode_field(cols[1]);
            if not path.startswith('/') or path in paths:
                raise MonoPodError('Invalid or duplicate track path')
            paths.add(path)
            rec=Track(path=path,file_size=int(cols[2]),modified_unix=int(cols[3]) or None,
                fingerprint_crc32_sample=cols[4],full_crc32=None if cols[5]=='00000000' else cols[5],
                duration_ms=int(cols[6]),year=int(cols[7]) or None,track_number=int(cols[8]) or None,
                sample_rate=int(cols[9]) or None,bitrate_kbps=int(cols[10]) or None,
                codec=_decode_field(cols[11]),extension=_decode_field(cols[12]),
                title=_decode_field(cols[13]),artist=_decode_field(cols[14]),album=_decode_field(cols[15]),
                album_artist=_decode_field(cols[16]),genre=_decode_field(cols[17]),
                flac_total_samples=int(cols[18]),flac_sample_rate=int(cols[19]))
            tracks.append(rec)
        return generation, tracks
    except (IndexError, ValueError, UnicodeDecodeError) as e:
        raise MonoPodError('Malformed MonoPodDB file') from e


class MonoPodClient:
    def __init__(self,host:str,timeout:int=30):
        self.host=host.removeprefix('http://').rstrip('/')
        if '/' in self.host or not self.host:
            raise ValueError('Host must be an IP or hostname with optional port')
        self.base=f'http://{self.host}'
        self.timeout=timeout

    def request(self,method:str,endpoint:str,params:dict[str,Any]|None=None,body:bytes|None=None):
        uri=self.base+endpoint
        if params:
            uri+='?'+urllib.parse.urlencode(params)
        req=urllib.request.Request(uri,data=body,method=method)
        try:
            with urllib.request.urlopen(req,timeout=self.timeout) as response:
                return response.read(),dict(response.headers)
        except urllib.error.HTTPError as e:
            raise MonoPodError(f'HTTP {e.code} {endpoint}: {e.read(300)!r}') from e
        except OSError as e:
            raise MonoPodError(f'Unable to connect: {e}') from e

    def info(self)->dict:
        return json.loads(self.request('GET','/api/library/info')[0])

    def refresh(self)->dict:
        return json.loads(self.request('POST','/api/library/refresh',body=b'')[0])

    def fetch_db(self)->tuple[int,list[Track]]:
        info=self.info()
        if info['dirty']:
            raise MonoPodError('Database is stale; call refresh before reading')
        total=int(info['size']); gen=int(info['generation'])
        if not 1<=total<=MAX_DB:
            raise MonoPodError(f'Invalid database size {total}')
        output=bytearray()
        for offset in range(0,total,16384):
            data,headers=self.request('GET','/api/library/db',{'offset':offset,'count':min(16384,total-offset)})
            crc=headers.get('X-CRC32')
            if len(data)!=min(16384,total-offset) or not crc or f'{zlib.crc32(data):08X}'!=crc.upper():
                raise MonoPodError('Corrupted database transfer chunk')
            if int(headers.get('X-DB-Generation',gen))!=gen:
                raise MonoPodError('Database changed while transferring; retry')
            output.extend(data)
        if self.info()['generation']!=gen:
            raise MonoPodError('Database generation changed; retry')
        db_gen,tracks=parse_db(bytes(output))
        if db_gen!=gen or len(tracks)!=info['count']:
            raise MonoPodError('Database info/contents mismatch')
        return gen,tracks

    def download_file(self,remote_path:str, local_path:Path, size:int):
        # Old Sync v4 /api/fs/download supports at most 16 KiB/request.
        local_path.parent.mkdir(parents=True,exist_ok=True)
        with local_path.open('wb') as out:
            for offset in range(0,size,16384):
                data,head=self.request('GET','/api/fs/download',{'path':remote_path,'offset':offset,'count':min(16384,size-offset)})
                crc=head.get('X-CRC32')
                if len(data)!=min(16384,size-offset) or not crc or f'{zlib.crc32(data):08X}' != crc.upper():
                    raise MonoPodError(f'Corrupt download at offset {offset}')
                out.write(data)
        if local_path.stat().st_size != size:
            raise MonoPodError('Download size mismatch')

    def upload_file(self,remote_path:str, local_path:Path):
        # Stream multipart body without reading a 200+ MiB FLAC into RAM.
        n=local_path.stat().st_size
        if n>0xffffffff:
            raise MonoPodError('Firmware only accepts files <4GiB')
        crc=0
        with local_path.open('rb') as src:
            while block:=src.read(1024*1024): crc=zlib.crc32(block,crc)
        boundary='monopoddb-file-778f902'
        before=(f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="audio.bin"\r\n'
                'Content-Type: application/octet-stream\r\n\r\n').encode()
        after=(f'\r\n--{boundary}--\r\n').encode()
        query=urllib.parse.urlencode({'path':remote_path,'size':n,'crc':f'{crc:08X}','replace':'1'})
        host,_,port=self.host.partition(':')
        conn=http.client.HTTPConnection(host,int(port) if port else 80,timeout=120)
        try:
            conn.putrequest('POST',f'/api/fs/upload?{query}')
            conn.putheader('Content-Type',f'multipart/form-data; boundary={boundary}')
            conn.putheader('Content-Length',str(len(before)+n+len(after)))
            conn.endheaders();conn.send(before)
            with local_path.open('rb') as src:
                while block:=src.read(16384):conn.send(block)
            conn.send(after)
            response=conn.getresponse();text=response.read(512).decode(errors='replace')
            if response.status!=200:raise MonoPodError(f'Upload failed: HTTP {response.status}: {text}')
        finally:conn.close()

    def edit(self,path:str,changes:dict[str,str]):
        gen,tracks=self.fetch_db()
        candidate=[t for t in tracks if t.path==path]
        if len(candidate)!=1:raise MonoPodError('Track not found in database')
        track=candidate[0]
        if track.extension not in ('.mp3','.flac'):
            raise MonoPodError('The reference editor supports MP3 and FLAC tags only')
        with tempfile.TemporaryDirectory(prefix='monopod-tags-') as temporary:
            local=Path(temporary)/('work'+track.extension)
            self.download_file(path,local,track.file_size)
            edit_tags(local,changes)
            self.upload_file(path,local)
        self.refresh()
        print('Metadata updated; database refreshed')


def edit_tags(local:Path,changes:dict[str,str]):
    try:
        if local.suffix.lower()=='.flac':
            from mutagen.flac import FLAC
            f=FLAC(local)
            for field,value in changes.items():
                key={'title':'TITLE','artist':'ARTIST','album':'ALBUM','album_artist':'ALBUMARTIST',
                    'year':'DATE','genre':'GENRE','track_number':'TRACKNUMBER'}[field]
                if value:f[key]=[value]
                elif key in f:del f[key]
            f.save()
        else:
            from mutagen.id3 import ID3, ID3NoHeaderError, TIT2, TPE1, TALB, TPE2, TDRC, TCON, TRCK
            try:tags=ID3(local)
            except ID3NoHeaderError:tags=ID3()
            factory={'title':TIT2,'artist':TPE1,'album':TALB,'album_artist':TPE2,
                     'year':TDRC,'genre':TCON,'track_number':TRCK}
            for field,value in changes.items():
                cls=factory[field]
                tags.delall(cls.FrameID)
                if value:tags.add(cls(encoding=3,text=[value]))
            tags.save(local,v2_version=4)
    except ImportError as e:
        raise MonoPodError('Metadata editing needs pip install mutagen') from e


def cli():
    ap=argparse.ArgumentParser(description='MonoPodDB v1 Wi-Fi reference client')
    ap.add_argument('--host',required=True,help='IP address of MonoPod in active Wi-Fi Sync')
    sp=ap.add_subparsers(dest='action',required=True)
    sp.add_parser('list');sp.add_parser('refresh');sp.add_parser('json')
    edit=sp.add_parser('edit');edit.add_argument('--path',required=True)
    for key in ('title','artist','album','album_artist','year','genre','track_number'):
        edit.add_argument('--'+key.replace('_','-'),dest=key)
    args=ap.parse_args()
    mp=MonoPodClient(args.host)
    if args.action=='refresh':print(mp.refresh());return
    if args.action=='edit':
        changes={key:getattr(args,key) for key in ('title','artist','album','album_artist','year','genre','track_number') if getattr(args,key) is not None}
        if not changes:ap.error('Provide at least one metadata field')
        mp.edit(args.path,changes);return
    gen,tracks=mp.fetch_db()
    if args.action=='json':print(json.dumps({'generation':gen,'tracks':[asdict(t) for t in tracks]},ensure_ascii=False,indent=2))
    else:
        print(f'MonoPodDB generation={gen} tracks={len(tracks)}')
        for t in tracks:print(f'{t.artist} - {t.title} | {t.album} | {t.duration_ms/1000:.1f}s | {t.path}')

if __name__=='__main__':
    try:cli()
    except MonoPodError as e:print(f'ERROR: {e}',file=sys.stderr);sys.exit(1)
