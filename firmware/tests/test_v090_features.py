from pathlib import Path
import sys, importlib.util, random
ROOT=Path(__file__).resolve().parents[1]
FW=ROOT/'DIY_iPod_Mono'

def src(name):return (FW/name).read_text()

def test_backlight_and_battery():
    cfg=src('Config.h');bat=src('Battery.cpp');disp=src('Display.cpp')
    assert 'TFT_BL_ACTIVE_LOW = false' in cfg
    assert 'BATTERY_SAMPLE_INTERVAL_MS = 60000UL' in cfg
    assert '>=Config::BATTERY_SAMPLE_INTERVAL_MS' in bat
    assert 'void Display::batteryIcon()' in disp
    assert 'if(pct>=75)return 4;' in disp

def test_volume_clamps_and_seek_modes():
    cfg=src('Config.h');ui=src('UI.cpp')
    assert 'MAX_VOLUME = 100' in cfg
    assert 'constrain(previous+delta*step,0,100)' in ui
    assert 'nowOverlay=NowOverlay::Seek' in ui
    assert 'overlayUpdatedAt>=5000UL' in ui
    assert 'player.seekPercent' in ui
    assert 'add("Volume: "' not in ui

def test_no_fullscreen_erase_per_second():
    disp=src('Display.cpp');ui=src('UI.cpp')
    assert 'GFXcanvas16(160,128)' in disp
    assert 'tft.drawRGBBitmap(OX,OY,buffer->getBuffer(),160,128)' in disp
    assert 'display.nowPlayingControls(' in ui
    assert '(page==Page::NowPlaying&&millis()-lastRender>1000UL)' not in ui
    assert 'if(nowOverlay==NowOverlay::Progress && player.elapsedSeconds()!=lastNowTime)' in ui

def test_boot_logo_and_video_paths():
    assert (FW/'BootLogoBitmap.h').exists()
    assert 'display.bootLogo()' in src('DIY_iPod_Mono.ino')
    assert 'boot.rgb565' in src('Display.cpp')
    assert 'EXCLUDE_BAD_APPLE_EASTEREGG' in src('Config.h')
    assert 'add("Bad Apple",3,false);' in src('UI.cpp')
    assert 'streamGray2Frame' in src('BadApple.cpp')
    assert (ROOT/'tools/convert_badapple.py').exists()

def test_badapple_delta_encoder_roundtrips():
    spec=importlib.util.spec_from_file_location('badapple_encode',ROOT/'tools/convert_badapple.py')
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
    rng=random.Random(13)
    prev=bytes(rng.randrange(256) for _ in range(5120));cur=bytes(rng.randrange(256) for _ in range(5120))
    code=m.encode_delta(prev,cur)
    output=bytearray(prev);i=j=0
    while j<len(code):
        c=code[j];j+=1;n=(c&127)+1
        if c&128:i+=n
        else:
            for k in range(n):output[i+k]^=code[j+k]
            i+=n;j+=n
    assert bytes(output)==cur and i==5120

def test_wokwi_shared_important_code():
    for name in ('Display.cpp','Display.h','UI.cpp','Battery.cpp','BadApple.cpp','BadApple.h'):
        assert (FW/name).read_bytes()==(ROOT/'Wokwi_ILI9341'/name).read_bytes(),name
