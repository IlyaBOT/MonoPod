from pathlib import Path
import struct
import sys
import pytest
ROOT=Path(__file__).resolve().parents[1]
P=ROOT/'DIY_iPod_Mono'

def test_protocol_v4_only():
    usb=(P/'UsbSync.cpp').read_text()
    wifi=(P/'WiFiSync.cpp').read_text()
    assert 'MPOD1 HELLO' in usb and 'READY 4' in usb
    assert 'MPOD1 X CLOCK ' in usb
    assert 'MPOD1 PUT ' not in usb and 'MPOD1 REPLACE ' not in usb
    assert 'MPOD1 DELETE ' not in usb and 'MPOD1 INFO"' not in usb
    assert 'server.on("/upload"' not in wifi
    assert 'server.on("/time"' not in wifi
    assert 'server.on("/api/clock",HTTP_POST' in wifi

def test_legacy_hardware_removed():
    code='\n'.join(f.read_text() for f in P.glob('*.h'))+'\n'+ '\n'.join(f.read_text() for f in P.glob('*.cpp'))
    for needle in ('WOKWI_SIMULATION','Adafruit_ILI9341','FEATURE_DEV_ENCODER','ENCODER_A'):
        assert needle not in code

def test_now_playing_status_icon():
    display=(P/'Display.cpp').read_text();ui=(P/'UI.cpp').read_text()
    assert 'if(nowPlaying)' in display
    assert 'fillTriangle(OX+7' in display
    assert 'else {surface().fillRect(OX+7' in display
    assert 'page==Page::NowPlaying?0:bars' in ui
    assert 'smallText(20,5,"H"' in display

def test_backlight_options():
    ui=(P/'UI.cpp').read_text()
    assert 'const uint8_t values[]={5,10,15,30,60};' in ui
    assert '1 Second' not in ui

def test_clock_ntp_and_local():
    c=(P/'ClockService.cpp').read_text();u=(P/'UsbSync.cpp').read_text()
    assert 'configTzTime' in c
    assert 'esp_sntp_stop' in c
    assert 'clockService.setUnix' in u
    assert 'monopod-clock' in c

def test_bav2_decoder_mp3():
    c=(P/'BadApple.cpp').read_text(); h=(P/'BadAppleAudioSource.h').read_text()
    assert '"BAV2"' in c and 'startAudio(path,audioOffset,audioSize)' in c
    assert 'position()+n>videoEnd' in c
    assert 'size_-cursor_' in h
    assert 'player.externalAudioSink()' in c

def test_bav_asset_when_present():
    p=ROOT/'badapple.bav'
    if not p.exists():pytest.skip('BAV2 binary distributed separately')
    with p.open('rb') as f:
        head=f.read(22)
        m,w,h,fps,n,ao,al,codec=struct.unpack('<4sHHBIIIB',head)
        assert m==b'BAV2' and (w,h)==(160,128) and codec==1 and 1<=fps<=25
        assert ao+al==p.stat().st_size
