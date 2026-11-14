"""Backlight regression tests, in addition to native fade math tests."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def test_s3_gpio15_and_no_conflicts():
    for folder in ('DIY_iPod_Mono','Wokwi_ILI9341'):
        cfg=(ROOT/folder/'Config.h').read_text()
        assert 'constexpr int8_t PIN_TFT_BL = 15;' in cfg
        assert 'constexpr int8_t PIN_TFT_BL = -1;' in cfg
        assert 'TFT_BL_PWM_HZ = 16000' in cfg
        assert 'TFT_BL_ACTIVE_LOW = true' in cfg
        assert 'S3 GPIO15 backlight PWM conflicts with developer encoder' in cfg
        assert 'constexpr uint8_t PIN_PIEZO = 42;' in cfg

def test_ui_has_brightness_and_idle_fade():
    for folder in ('DIY_iPod_Mono','Wokwi_ILI9341'):
        ui=(ROOT/folder/'UI.cpp').read_text()
        assert 'case Page::Backlight:' in ui
        assert 'backlight.wake();' in ui
        assert 'backlight.sleep();' in ui
        assert 'backlight.setBrightness(settings.data.backlightBrightness);' in ui
        assert 'void PlayerUI::loop(){\n  if(syncMode.active()){' in ui
        assert 'blank=true;backlight.sleep();' in ui
        assert 'backlight.loop();' in ui
        assert 'settings.data.backlightBrightness' in ui
        st=(ROOT/folder/'Settings.cpp').read_text()
        assert 'pref.getUChar("blpct",75)' in st
        assert 'pref.putUChar("blpct",data.backlightBrightness)' in st

def test_driver_nonblocking_and_ledc():
    p=(ROOT/'DIY_iPod_Mono/Backlight.cpp').read_text()
    assert 'delay(' not in p
    assert 'ledcAttach(' in p and 'ledcWrite(' in p
    assert 'BacklightMath::gammaDuty(percent)' in p
    assert 'BacklightMath::ease(elapsed,fadeMs)' in p
    assert 'if(desiredOn==on && target==next)return;' in p
