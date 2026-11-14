from pathlib import Path
P = Path(__file__).resolve().parents[1] / "DIY_iPod_Mono"

def test_actual_s3_button_ladder_and_version_unchanged():
    c = (P / "Config.h").read_text()
    i = (P / "Input.cpp").read_text()
    assert "PREV=5k1, NEXT=10k, PLAY=20k, SELECT=39k9" in c
    assert "decodeButtonMillivolts" in i and "buttonAdcMillivolts" in i
    assert "BUTTON_ADC_INTERNAL_PULLUP = true;" in c
    assert "BUTTON_ADC_INTERNAL_PULLUP = false;" in c
    assert "BUTTON_ADC_WINDOWS_MV[5][2]" in c
    assert "BUTTON_ADC_BOUNDS_MV[5] = {200, 820, 1290, 1840, 2580};" in c

def test_nominal_button_voltages_classify_as_expected():
    windows=[(0,120),(180,480),(490,830),(845,1330),(1360,2100)]
    buttons=[(0,0),(5100,1),(10000,2),(20000,3),(39900,4)]
    for resistor, expected in buttons:
        mv=3300*resistor/(45000+resistor)
        matching=[i for i,(lo,hi) in enumerate(windows) if lo<=mv<=hi]
        assert matching == [expected], (resistor,mv,matching)
    for value in [145,485,835,1345,2200,3000,3300]:
        assert not any(lo<=value<=hi for lo,hi in windows)

def test_windows_never_overlap():
    windows=[(0,120),(180,480),(490,830),(845,1330),(1360,2100)]
    assert all(windows[i][1]<windows[i+1][0] for i in range(4))
