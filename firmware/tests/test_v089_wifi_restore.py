"""Regression checks for Wi-Fi NVS restoration and icon isolation."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def get(folder,name):return (ROOT/folder/name).read_text(encoding='utf-8')

def test_boot_reconnect_and_separate_nvs_namespace():
    for folder,entry in (('DIY_iPod_Mono','DIY_iPod_Mono.ino'),('Wokwi_ILI9341','sketch.ino')):
        sketch=get(folder,entry)
        assert 'wifiNetworks.begin();' in sketch
        assert sketch.index('wifiNetworks.begin();') < sketch.index('ui.begin();')
        wifi=get(folder,'WiFiNetworks.cpp')
        assert 'p.begin("monopod-wifi",true)' in wifi
        assert 'savedSSID=p.getString("ssid","")' in wifi
        assert 'savedPassword=p.getString("pass","")' in wifi
        assert 'beginConnection(savedSSID,savedPassword,false)' in wifi
        assert 'if(pendingSave)saveCredentials();' in wifi
        assert 'WiFi.setAutoReconnect(false)' in wifi
        assert 'kRetryDelayMs=15000' in wifi
        assert 'void WiFiNetworkManager::tick(){' in wifi
        assert wifi.index('if(!active)return;',wifi.index('void WiFiNetworkManager::tick(){')) > wifi.index('beginConnection(savedSSID,savedPassword,false);',wifi.index('void WiFiNetworkManager::tick(){'))

def test_ap_sync_exclusive_and_credentials_disconnect():
    for folder in ('DIY_iPod_Mono','Wokwi_ILI9341'):
        wifi=get(folder,'WiFiNetworks.cpp'); sync=get(folder,'WiFiSync.cpp')
        assert 'p.remove("ssid")' in wifi and 'p.remove("pass")' in wifi
        assert 'wifiNetworks.suspendForSync()' in sync
        assert sync.count('wifiNetworks.resumeAfterSync()')>=2
        assert 'if(suspended || syncMode.active())return;' in wifi

def test_wifi_icon_is_three_bars_and_only_region_repainted():
    for folder in ('DIY_iPod_Mono','Wokwi_ILI9341'):
        display=get(folder,'Display.cpp'); ui=get(folder,'UI.cpp')
        assert 'void Display::wifiSignal(uint8_t bars)' in display
        assert 'surface().fillRect(OX+2,OY,17,19,bg);' in display
        assert 'wifiSignal(wifiBars);' in display
        assert 'smallText(117,3,"W"' not in display
        assert 'display.wifiSignal(next==5?3:(next==4?0:next));' in ui
        assert 'const bool wifiChanged=wifiNetworks.changed()' in ui
        assert 'if(!blank && !dirty)' in ui

def test_refresh_interval_and_blink_and_signal():
    for folder in ('DIY_iPod_Mono','Wokwi_ILI9341'):
        wifi=get(folder,'WiFiNetworks.cpp')
        assert 'kRssiIntervalMs=12000' in wifi
        assert 'kBlinkIntervalMs=650' in wifi
        assert 'WiFi.RSSI()' in wifi
        assert 'WiFiSignal::barsForRssi(' in wifi
        assert 'if(connected())return cachedBars;' in wifi
        assert 'if(connectRunning)return' in wifi
