from pathlib import Path
R=Path(__file__).resolve().parents[1]/"DIY_iPod_Mono"

def test_click_counts_rows_after_clamping():
    ui=(R/'UI.cpp').read_text()
    adjust=ui.split('void PlayerUI::adjust(',1)[1].split('static ',1)[0]
    movement=adjust.split('selected=constrain(selected+delta*(fast?3:1),0,count-1);',1)[1]
    assert 'if(selected!=before){\n    feedback.cursorMoved((uint32_t)abs(selected-before));' in movement

def test_hold_lock_returns_before_waking_backlight():
    ui=(R/'UI.cpp').read_text()
    before_wake=ui.split('void PlayerUI::event(',1)[1].split('backlight.wake()',1)[0]
    hold_lock=before_wake.split('if(key==Key::HoldChanged && input.held()){',1)[1].split('}',1)[0]
    assert 'return;' in hold_lock
    assert 'blank=' not in hold_lock

def test_music_decoder_blocks_headphone_clicks():
    audio=(R/'AudioEngine.cpp').read_text()
    click=audio.split('void AudioEngine::headphoneClick(',1)[1].split('void ',1)[0]
    assert '!backgroundSuspended_ && (!decoder || pausedState)' in click

def test_speed_accel_keeps_slow_motion_one_unit():
    ui=(R/'UI.cpp').read_text()
    assert 'wheelBurst>=2' in ui
    assert 'now-lastWheelTurnAt)<130' in ui
    assert 'delta*(fast?3:1)' in ui
    assert 'delta*(fast?5:1)' in ui
    assert 'delta*(fast?30:1)' in ui

def test_exact_second_scrub_and_saturating_volume():
    ui=(R/'UI.cpp').read_text()
    audio=(R/'AudioEngine.cpp').read_text()
    display=(R/'Display.cpp').read_text()
    assert 'player.seekSeconds(seekPreviewSeconds)' in ui
    assert 'min(seconds,duration-1)' in audio
    assert 'seekBaseSeconds=seconds' in audio
    assert 'constrain(previous+delta*(fast?5:1),0,100)' in ui
    assert 'gain=gain*sqrtf(gain)' in audio
    assert 'scrubSeconds==UINT32_MAX' in display
