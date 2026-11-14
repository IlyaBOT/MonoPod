"""Wi-Fi Sync on STA LAN with AP fallback, explicit sync-mode and proper routing."""
from pathlib import Path
import pytest
from Studio.device_api import DeviceClient

ROOT=Path(__file__).resolve().parents[1]

@pytest.mark.parametrize('folder',['DIY_iPod_Mono','Wokwi_ILI9341'])
def test_lan_when_station_is_already_connected(folder):
    base=ROOT/folder
    sync=(base/'WiFiSync.cpp').read_text()
    networks=(base/'WiFiNetworks.cpp').read_text()
    assert 'usingSta=WiFi.status()==WL_CONNECTED;' in sync
    assert 'if(WiFi.status()!=WL_CONNECTED&&wifiNetworks.connecting())' in sync
    assert 'if(!usingSta){' in sync
    assert 'wifiNetworks.suspendForSync();' in sync
    assert 'WiFi.mode(WIFI_AP);' in sync
    assert 'if(!usingSta)wifiNetworks.resumeAfterSync();' in sync
    assert 'if(usingSta)wifiNetworks.tick();' in sync
    assert 'if(suspended || (syncMode.active() && !syncMode.is(SyncTransport::WiFi)))return;' in networks
    assert 'usingSta?WiFi.localIP():WiFi.softAPIP()' in sync
    assert 'if(!running||busy())return;' in sync
    assert 'server.begin();running=true;' in sync
    assert sync.index('syncMode.enter(SyncTransport::WiFi)') < sync.index('server.begin();running=true;')
    assert 'if(!usingSta){WiFi.softAPdisconnect(true);WiFi.mode(WIFI_OFF);' in sync

@pytest.mark.parametrize('folder',['DIY_iPod_Mono','Wokwi_ILI9341'])
def test_sync_screen_displays_real_ip(folder):
    base=ROOT/folder
    assert 'wifiSync.ip():String()' in (base/'UI.cpp').read_text()
    assert 'smallText(3,91,"IP: "+address,color,back);' in (base/'Display.cpp').read_text()


def test_device_client_unreachable_hint():
    import socket
    s=socket.socket();s.bind(('127.0.0.1',0));port=s.getsockname()[1];s.close()
    with pytest.raises(ConnectionError,match='Start Wi-Fi Sync'):
        DeviceClient(f'http://127.0.0.1:{port}').status()

