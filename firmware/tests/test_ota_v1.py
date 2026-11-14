import hashlib
import http.server
import importlib.util
import json
from pathlib import Path
import socketserver
import threading

ROOT = Path(__file__).resolve().parents[1]
FW = ROOT / 'DIY_iPod_Mono'


def test_ota_wired_only_to_wifi_sync():
    wifi = (FW / 'WiFiSync.cpp').read_text()
    ota = (FW / 'WiFiOta.cpp').read_text()
    assert 'server.on("/api/ota/info",HTTP_GET' in wifi
    assert 'server.on("/api/ota/upload",HTTP_POST' in wifi
    assert 'server.collectHeaders(otaHeaders,4)' in wifi
    assert 'wifiOta.newSession();server.begin()' in wifi
    assert 'esp_ota_get_next_update_partition(nullptr)' in ota
    assert 'ESP_PARTITION_SUBTYPE_APP_OTA_0' in ota
    assert 'ESP_PARTITION_SUBTYPE_APP_OTA_1' in ota
    assert 'ESP_PARTITION_SUBTYPE_DATA_OTA' in ota
    assert 'WRONG_CHIP_OR_NOT_APP_IMAGE' in ota
    assert 'mbedtls_md_update' in ota
    assert 'Update.end(false)' in ota
    assert 'esp_random()%900000u' in ota
    assert 'badPinAttempts>=5' in ota
    assert 'wifiOta.abort("CLIENT_ABORTED")' in wifi
    assert 'TRANSFER_TIMEOUT' in ota
    assert '0.10.7.5' in (FW / 'DIY_iPod_Mono.ino').read_text()


def test_desktop_reference_posts_stream_and_hash(tmp_path):
    src = ROOT / 'tools' / 'monopod_ota_client.py'
    spec = importlib.util.spec_from_file_location('monopod_ota_client', src)
    client = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(client)
    file_path = tmp_path / 'firmware.bin'
    file_path.write_bytes(b'\xe9' + b'X' * 39999)
    seen = {}

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            assert self.path == '/api/ota/info'
            body = json.dumps({'supported': True, 'max_image_bytes': 7340032}).encode()
            self.send_response(200)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_POST(self):
            assert self.path == '/api/ota/upload'
            raw = self.rfile.read(int(self.headers['Content-Length']))
            payload = raw[raw.index(b'\r\n\r\n')+4:raw.rindex(b'\r\n--')]
            seen['bytes'] = payload
            seen['sha'] = self.headers['X-MonoPod-OTA-SHA256']
            seen['size'] = self.headers['X-MonoPod-OTA-Size']
            seen['pin'] = self.headers['X-MonoPod-OTA-PIN']
            body = b'{"ok":true,"status":"verified","rebooting":true}'
            self.send_response(200)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *_args):
            pass

    class ThreadServer(socketserver.ThreadingTCPServer):
        allow_reuse_address = True

    with ThreadServer(('127.0.0.1', 0), Handler) as server:
        th = threading.Thread(target=server.serve_forever, daemon=True)
        th.start()
        code, body = client.post_firmware(f'http://127.0.0.1:{server.server_address[1]}', file_path, '123456')
        server.shutdown()
    assert code == 200
    assert json.loads(body)['rebooting']
    assert seen['bytes'] == file_path.read_bytes()
    assert seen['sha'] == hashlib.sha256(seen['bytes']).hexdigest()
    assert seen['size'] == str(file_path.stat().st_size)
    assert seen['pin'] == '123456'
