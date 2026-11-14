"""Local Wi-Fi (not Internet) device management. No third-party HTTP deps."""
import json
import http.client
from pathlib import Path
from urllib.parse import urlencode, urlparse
from urllib.request import Request, urlopen
import uuid

class DeviceClient:
    def __init__(self,url='http://192.168.4.1'):
        self.url=url.rstrip('/')
        parsed=urlparse(self.url)
        if parsed.scheme!='http' or not parsed.hostname:
            raise ValueError('Use a local HTTP device address, such as http://192.168.4.1')
        self.host=parsed.hostname
        self.port=parsed.port or 80
        self.prefix=parsed.path.rstrip('/')

    def status(self):
        with urlopen(Request(self.url+'/api/info'),timeout=8) as response:
            return json.loads(response.read(16384))

    def form(self,endpoint,data):
        body=urlencode(data).encode('ascii')
        req=Request(self.url+endpoint,body,{'Content-Type':'application/x-www-form-urlencoded'},method='POST')
        with urlopen(req,timeout=15) as response:
            return response.read(2048).decode('utf8','replace')

    def upload(self,endpoint,path,field='theme',progress=None):
        path=Path(path)
        # Boundary controlled by the client; no user-controlled request path.
        bound='monopod-'+uuid.uuid4().hex
        safe_filename=path.name.replace('"','').replace('\\','_').replace('\r','').replace('\n','')
        pre=(f'--{bound}\r\nContent-Disposition: form-data; name="{field}"; filename="{safe_filename}"\r\n'
             f'Content-Type: application/octet-stream\r\n\r\n').encode()
        tail=(f'\r\n--{bound}--\r\n').encode()
        total=path.stat().st_size
        conn=http.client.HTTPConnection(self.host,self.port,timeout=45)
        try:
            conn.putrequest('POST',self.prefix+endpoint)
            conn.putheader('Content-Type',f'multipart/form-data; boundary={bound}')
            conn.putheader('Content-Length',str(len(pre)+total+len(tail)))
            conn.endheaders()
            conn.send(pre)
            sent=0
            with path.open('rb') as f:
                while True:
                    chunk=f.read(32768)
                    if not chunk:break
                    conn.send(chunk);sent+=len(chunk)
                    if progress:progress(sent,total)
            conn.send(tail)
            response=conn.getresponse()
            message=response.read(8192).decode('utf8','replace')
            if response.status>=400:raise RuntimeError(f'HTTP {response.status}: {message}')
            return message
        finally:
            conn.close()
