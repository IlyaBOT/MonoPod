from pathlib import Path
import re
P = Path(__file__).resolve().parents[1] / "DIY_iPod_Mono"

def test_actual_s3_button_ladder_and_external_20k():
    cfg=(P/"Config.h").read_text()
    inp=(P/"Input.cpp").read_text()
    assert "MENU=0, PREV=5.1k, NEXT=10k, PLAY=20k, SELECT=39.9k" in cfg
    assert "BUTTON_ADC_INTERNAL_PULLUP = false;" in cfg
    assert "pinMode(Config::PIN_BUTTON_ADC, INPUT);" in inp
    assert "INPUT_PULLUP" not in inp
    assert "decodeButtonMillivolts" in inp

windows=[(0,120),(530,800),(940,1220),(1480,1840),(2040,2410)]

def test_nominal_button_voltages_classify_as_expected():
    for expected, r in enumerate([0,5100,10000,20000,39900]):
        mv=3300*r/(20000+r)
        matching=[i for i,(lo,hi) in enumerate(windows) if lo<=mv<=hi]
        assert matching == [expected], (r,mv,matching)
    for value in [200,850,1300,1940,2700,3148,3300]:
        assert not any(lo<=value<=hi for lo,hi in windows)

def test_windows_never_overlap():
    assert all(windows[i][1]<windows[i+1][0] for i in range(4))
