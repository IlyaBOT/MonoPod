from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]/"DIY_iPod_Mono"

def test_charging_state_and_render():
    cfg=(ROOT/"Config.h").read_text()
    batt=(ROOT/"Battery.cpp").read_text()
    disp=(ROOT/"Display.cpp").read_text()
    assert "BATTERY_CHARGE_ADC_ON_MV = 2150" in cfg
    assert "BATTERY_CHARGE_ADC_OFF_MV = 2130" in cfg
    assert "adcMv>Config::BATTERY_CHARGE_ADC_ON_MV" in batt
    assert "if(battery.charging())return 5;" in disp
    assert "if(b==5){ // charging" in disp

def test_all_version_surfaces_consistent():
    assert 'MonoPod v0.10.7.4 /' in (ROOT/"DIY_iPod_Mono.ino").read_text()
    assert 'case 3:value="0.10.7.4";' in (ROOT/"UI.cpp").read_text()
    assert r'\"firmware\":\"0.10.7.4\"' in (ROOT/"SyncFiles.cpp").read_text()
