from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1] / "DIY_iPod_Mono"


def test_about_sn_starts_after_two_spaces_and_is_not_ellipsized():
    ui = (ROOT / "UI.cpp").read_text()
    disp = (ROOT / "Display.cpp").read_text()
    header = (ROOT / "Display.h").read_text()
    assert 'const int serialX=6+display.textWidth(entry.title,true)+display.textWidth("  ",true);' in ui
    assert 'display.serialText(serialX,rowY,value,' in ui
    assert 'void Display::serialText(' in disp and 'void serialText(' in header
    assert 'surface().setTextSize(1);' in disp
    assert 'surface().setTextWrap(false);' in disp
    assert 'surface().print(serial);' in disp
    # MonoPod's 12px label is 24px wide, two spaces are 2*4px,
    # and the built-in font is 6px wide per ASCII character.
    serial_x = 6 + 24 + 2 * 4
    assert serial_x + 16 * 6 < 160


def test_now_playing_home_entry_only_when_actively_playing():
    ui = (ROOT / "UI.cpp").read_text()
    assert 'if(player.active() && !player.paused())add("Now Playing",5,true);' in ui
    assert 'case 5:if(player.active() && !player.paused())navigate(Page::NowPlaying);break;' in ui
    assert 'if(playState!=lastPlaybackState)' in ui
    assert 'if(page==Page::Home){build();}' in ui
    assert 'if(selected>=listItemCount())selected=max(0,listItemCount()-1);' in ui


def test_version_and_actual_adc_ladder():
    ui = (ROOT / "UI.cpp").read_text()
    conf = (ROOT / "Config.h").read_text()
    assert 'case 3:value="0.10.7.2";' in ui
    assert 'MENU=0, PREV=5.1k, NEXT=10k, PLAY=20k, SELECT=39.9k' in conf
    assert 'ButtonAdcCompensation.h' in conf
