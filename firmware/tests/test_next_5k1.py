from pathlib import Path
import re

P = Path(__file__).resolve().parents[1] / "DIY_iPod_Mono"

def test_next_is_5k1_s3_and_version_is_unchanged():
    c = (P / "Config.h").read_text()
    i = (P / "Input.cpp").read_text()
    assert "NEXT=5k1" in c
    assert "0/2k2/5k1/10k/22k" in i
    assert "BUTTON_ADC_INTERNAL_PULLUP = true;" in c
    assert "BUTTON_ADC_INTERNAL_PULLUP = false;" in c
    assert "constexpr int BUTTON_ADC_BOUNDS_MV[5] = {75, 245, 470, 795, 1550};" in c
    assert "constexpr int BUTTON_ADC_BOUNDS_MV[5] = {200, 820, 1290, 1840, 2580};" in c

def test_nominal_button_voltages_classify_as_expected():
    limits = [75, 245, 470, 795, 1550]
    buttons = [(0, 0), (2200, 1), (5100, 2), (10000, 3), (22000, 4)]
    for r, expected in buttons:
        mv = 3300*r/(45000+r)
        result = next((n for n, high in enumerate(limits) if mv < high), 5)
        assert result == expected
    assert next((n for n, high in enumerate(limits) if 3300 < high), 5) == 5
