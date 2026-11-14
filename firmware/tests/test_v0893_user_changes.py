"""User-requested 2004 UI and non-looping scroll click, FAT formatting regressions."""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]

def source(name):return (ROOT / 'DIY_iPod_Mono' / name).read_text()

def test_status_centers_and_elides_titles():
    s=source('Display.cpp')
    assert 'const int x=(160-w)/2;' in s
    assert 'String Display::fittedTitle(' in s
    assert 'return result+dots;' in s
    assert 'const int bx=136' in s

def test_scroll_only_click_and_i2s_goes_idle():
    ui, fb, mixer=source('UI.cpp'),source('Feedback.cpp'),source('ClickMixOutput.cpp')
    assert 'if(key==Key::Up || key==Key::Down)feedback.fire(FeedbackEvent::Scroll);' in ui
    assert 'feedback.fire(FeedbackEvent::Select)' not in ui
    assert 'feedback.fire(FeedbackEvent::Menu)' not in ui
    assert 'if(event!=FeedbackEvent::Scroll || selected==ClickerMode::Off)return;' in fb
    assert 'target_->stop();started_=false;' in mixer
    assert 'duration_-remaining_' in mixer

def test_fat_only_double_confirm_and_dirs():
    lib,ui,cfg=source('Library.cpp'),source('UI.cpp'),source('Config.h')
    assert 'FatFormatter formatter;' in lib and 'ExFatFormatter formatter;' not in lib
    assert 'SD.mkdir("/Music")' in lib and 'SD.mkdir("/Playlists")' in lib
    assert 'if(page==Page::StorageFormat1){navigate(Page::StorageFormat2);return;}' in ui
    assert 'FEATURE_SD_FORMAT (!WOKWI_SIMULATION)' in cfg

def test_settings_apply_now_and_original_menus():
    s=source('UI.cpp')
    assert 'case Page::BacklightTimer:' in s
    assert 'add("Reset All Settings",13);' in s
    assert 'settings.resetDefaults();' in s
    assert 'feedback.setMode(settings.data.clicker);' in s
    assert 'backlight.setBrightness(settings.data.backlightBrightness);' in s
    assert 'page==Page::About' in s
