"""Sync-mode regression tests; Arduino full-build/hardware testing is separate."""
from pathlib import Path
import tempfile
import zlib
from Studio.usb_sync import UsbClient
ROOT=Path(__file__).resolve().parents[1]

def test_sync_controller_both_board_profiles():
    for profile in ('DIY_iPod_Mono','Wokwi_ILI9341'):
        p=ROOT/profile
        ctrl=(p/'SyncMode.cpp').read_text()
        assert 'player.stop();' in ctrl
        assert ctrl.index('player.stop();') < ctrl.index('current=wanted;')
        assert 'feedback.quiet();' in ctrl
        ui=(p/'UI.cpp').read_text()
        assert 'if(syncMode.active()){' in ui
        assert 'display.syncScreen(' in ui
        assert 'key==Key::SelectLong && !wifiSync.busy()' in ui
        assert 'wifiNetworks.tick();' in ui
        assert ui.index('if(syncMode.active()){\n    // Never blank') < ui.index('wifiNetworks.tick();')
        screen=(p/'Display.cpp').read_text()
        for message in ('SYNC','DO NOT REMOVE SD','DO NOT UNPLUG USB','KEEP Wi-Fi CONNECTED'):
            assert message in screen

def test_usb_gated_transfers_and_explicit_handshake():
    src=(ROOT/'DIY_iPod_Mono/UsbSync.cpp').read_text()
    assert 'MPOD1 SYNC START' in src and 'MPOD1 SYNC END' in src
    assert 'ERR SYNC_REQUIRED' in src
    assert 'ERR TRANSFER_ACTIVE' in src
    assert src.index('ERR SYNC_REQUIRED') < src.index('MPOD1 DELETE')
    client=(ROOT/'Studio/usb_sync.py').read_text()
    assert "self.send('SYNC START')" in client
    assert "self.send('SYNC END')" in client
    assert client.index('self.start_sync()',client.index('    def upload(')) < client.index("self.send(f'",client.index('    def upload('))

def test_wifi_session_started_before_ap_and_no_exit_during_upload():
    src=(ROOT/'DIY_iPod_Mono/WiFiSync.cpp').read_text()
    assert src.index('syncMode.enter(SyncTransport::WiFi)') < src.index('WiFi.softAP(')
    assert 'if(!running || busy())return;' in src
    assert 'uploadActive=false;' in src
    assert '"/Music/.monopod-wifi.target"' in src
    assert 'SD.rename(part,dest.c_str())' in src
    assert 'WiFi.mode(WIFI_OFF);syncMode.leave(SyncTransport::WiFi)' in src

def test_usb_connection_info_does_not_enter_sync():
    class Port:
        def __init__(self): self.cmds=[];self.answers=[]
        def write(self,wire):
            cmd=wire.decode().strip();self.cmds.append(cmd)
            if cmd=='MPOD1 HELLO': self.answers.append(b'MPOD1 READY 2 SD=1\n')
            elif cmd=='MPOD1 INFO':self.answers.append(b'MPOD1 OK INFO SD=1 TRACKS=0\n')
            else:raise AssertionError(f'unexpected write {cmd}')
            return len(wire)
        def readline(self): return self.answers.pop(0) if self.answers else b''
    port=Port()
    with UsbClient('FAKE',transport=port) as device:
        assert 'SD=1' in device.info()
    assert port.cmds==['MPOD1 HELLO','MPOD1 INFO']
