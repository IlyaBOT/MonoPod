from pathlib import Path
p=Path(__file__).resolve().parents[1] / "DIY_iPod_Mono"
input_cpp=(p/"Input.cpp").read_text()
input_h=(p/"Input.h").read_text()
console=(p/"DevConsole.cpp").read_text()
wheel=(p/"WheelDetents.h").read_text()

def test_touch_debug_and_manual_calibration_removed():
    for token in ("$TOUCH_DEBUG", "$TOUCH_ENABLE", "$TOUCH_CAL", "$TOUCH_SENS", "$TOUCH_TOTAL", "$TOUCH_MIN", "$TOUCH_DOM", "$TOUCH_PAD", "$BTNS_ADC", "$BUTTON_ADC"):
        assert token not in console
    assert "touchCalibrate()" in input_cpp and "touchCalibrate();" in input_cpp
    for name in ("touchSetSensitivity", "touchSetPadSensitivity", "touchEnable"):
        assert name not in input_cpp and name not in input_h

def test_fractional_angle_integration_and_no_hard_deadband():
    assert "(uint32_t)(now-touchCandidateSince)<35" in input_cpp
    assert "wheelIntegrator.sample(measured)" in input_cpp
    assert "fraction_+=filtered;" in wheel
    assert "fabsf(d)<0.08f" not in input_cpp
    assert "b!=Key::None || keyStable!=Key::None" in input_cpp

def test_automatic_calibration_and_dropout_bridge_stay():
    assert "touchCalibrate();" in input_cpp
    assert "badBaselineSamples>=4" in input_cpp
    assert "(now-touchMissingSince)>130" in input_cpp
    assert "TOUCH_SENS_PCT=3" in input_h
    assert "TOUCH_TOTAL_PCT=2" in input_h

def test_both_board_profiles_preserved():
    cfg=(p/"Config.h").read_text()
    assert "MONOPOD_BOARD_ESP32_S3" in cfg
    assert "MONOPOD_BOARD_ESP32" in cfg
    assert "constexpr uint8_t WHEEL_PINS[6] = {1,2,5,6,7,8}" in cfg
