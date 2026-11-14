from pathlib import Path
R=Path(__file__).resolve().parents[1]/"DIY_iPod_Mono"

def test_each_detent_has_click_without_rate_limit():
    feedback=(R/'Feedback.cpp').read_text()
    assert "if(lastScrollAt && now-lastScrollAt<" not in feedback
    assert "startTone(Config::PIEZO_SCROLL_HZ,3)" in feedback
    assert "feedback.fire(FeedbackEvent::Scroll)" in (R/'UI.cpp').read_text()

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

def test_three_version_surfaces():
    assert 'MonoPod v0.10.6.9 /' in (R/'DIY_iPod_Mono.ino').read_text()
    assert 'case 3:value="0.10.6.9";' in (R/'UI.cpp').read_text()
    assert r'\"firmware\":\"0.10.6.9\"' in (R/'SyncFiles.cpp').read_text()
