import unittest
from threading import Thread
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parent.parent/'Studio'))
from device_api import DeviceClient

class Handler(BaseHTTPRequestHandler):
    def log_message(self,*args):pass
    def do_GET(self):
        self.send_response(200);self.send_header('Content-Type','application/json');self.end_headers()
        self.wfile.write(b'{"firmware":"MonoPod v0.6","tracks":3}')
    def do_POST(self):
        length=int(self.headers['Content-Length']);body=self.rfile.read(length)
        if self.path=='/api/theme' and b'MPT1' not in body:self.send_response(400)
        elif self.path=='/api/smoothing' and b'enabled=1' not in body:self.send_response(400)
        else:self.send_response(200)
        self.end_headers();self.wfile.write(b'OK')

class DeviceAPITest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
        cls.thread=Thread(target=cls.server.serve_forever,daemon=True);cls.thread.start()
        cls.client=DeviceClient(f'http://127.0.0.1:{cls.server.server_address[1]}')
    @classmethod
    def tearDownClass(cls):cls.server.shutdown();cls.server.server_close()
    def test_status(self):self.assertEqual(self.client.status()['tracks'],3)
    def test_form(self):self.assertEqual(self.client.form('/api/smoothing',{'enabled':'1'}),'OK')
    def test_upload(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'test.mpt';p.write_bytes(b'MPT1'+b'\x00'*40)
            self.assertEqual(self.client.upload('/api/theme',p),'OK')

if __name__=='__main__':unittest.main()
