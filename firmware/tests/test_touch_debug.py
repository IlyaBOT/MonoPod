from pathlib import Path
p=Path(__file__).resolve().parents[1] / "DIY_iPod_Mono"
input_cpp=(p/"Input.cpp").read_text()
input_h=(p/"Input.h").read_text()
console=(p/"DevConsole.cpp").read_text()

def test_touch_runtime_api_and_commands():
    for token in ("$TOUCH_DEBUG=1", "$TOUCH_ENABLE=", "$TOUCH_CAL", "$TOUCH_SENS=", "$TOUCH_TOTAL=", "$TOUCH_MIN=", "$TOUCH_DOM=", "$TOUCH_PAD="):
        assert token in console
    for token in ("touchCalibrate", "touchSample", "touchEnable", "touchSetSensitivity", "touchSetPadSensitivity"):
        assert token in input_cpp and token in input_h

def test_no_single_sample_scroll():
    assert "(uint32_t)(now-touchCandidateSince)<35" in input_cpp
    assert "fabsf(d)<0.08f" in input_cpp
    assert "b!=Key::None || keyStable!=Key::None" in input_cpp

def test_disabled_touch_keeps_diagnostics():
    assert "if(!touchOn){touchResetTracking();return Key::None;}" in input_cpp
    assert "input.touchSample();" in console
    assert "TOUCH_EVENT=" in console

def test_both_board_profiles_preserved():
    config=(p/"Config.h").read_text()
    assert "MONOPOD_BOARD_ESP32_S3" in config
    assert "MONOPOD_BOARD_ESP32" in config
    assert "constexpr uint8_t WHEEL_PINS[6] = {1,2,5,6,7,8}" in config


def test_bridged_touch_dropout_and_s3_threshold():
    assert "touchMissingSince" in input_cpp
    assert "(now-touchMissingSince)>130" in input_cpp
    assert "touchSensitivityPct=3" in input_h
    assert "touchTotalPct=2" in input_h
