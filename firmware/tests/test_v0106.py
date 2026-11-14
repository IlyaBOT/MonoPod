from pathlib import Path
P=Path(__file__).resolve().parents[1]/'DIY_iPod_Mono'

def test_s3_button_pullup_and_original_branch_preserved():
    c=(P/'Config.h').read_text()
    i=(P/'Input.cpp').read_text()
    assert c.count('BUTTON_ADC_INTERNAL_PULLUP = false;')==2
    assert 'INPUT_PULLUP' not in i
    assert 'pinMode(Config::PIN_BUTTON_ADC, INPUT);' in i
    assert 'BUTTON_ADC_WINDOWS_MV' in i

def test_background_audio_status_and_home_menu():
    ino=(P/'DIY_iPod_Mono.ino').read_text()
    ui=(P/'UI.cpp').read_text()
    display=(P/'Display.cpp').read_text()
    assert 'player.loop();' in ino
    assert 'if(player.active() && !player.paused())add("Now Playing",5,true);' in ui
    assert 'if(playState!=lastPlaybackState)' in ui
    assert 'if(playbackPaused)' in display
    assert 'else if(playbackActive)' in display
    assert 'page!=Page::NowPlaying && (!player.active() || input.held())' in ui

def test_badapple_transfer_path_explicitly_whitelisted():
    code=(P/'SyncFiles.cpp').read_text()
    assert 'low=="/monopod/badapple.bav"' in code
    assert 'low=="/monopod"' in code
    assert 'low=="/monopod/theme.mpt"' in code
    assert 'protectedRoot(String p)' in code
    assert 'low.startsWith("/monopod/")' in code
