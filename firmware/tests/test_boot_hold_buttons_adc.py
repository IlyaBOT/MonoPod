from pathlib import Path
R=Path(__file__).resolve().parents[1]/"DIY_iPod_Mono"

def test_hold_does_not_mask_wifi():
    display=(R/"Display.cpp").read_text()
    ui=(R/"UI.cpp").read_text()
    assert "if(held){" in display
    assert "if(!nowPlaying)wifiSignal(wifiBars);" in display
    assert "page!=Page::NowPlaying && (!player.active() || input.held())" in ui

def test_boot_logo_scans_after_splash_and_wifi_starts_early():
    ino=(R/"DIY_iPod_Mono.ino").read_text()
    assert ino.index("media.begin(false)") < ino.index("display.begin()")
    assert ino.index("display.bootLogo()") < ino.index("media.rescan()")
    assert ino.index("wifiNetworks.begin()") < ino.index("media.rescan()")
    assert ino.index("clockService.begin()") < ino.index("wifiNetworks.begin()")
    assert "BOOT_LOGO_MIN_MS = 1850UL" in (R/"Config.h").read_text()
    assert "appleBootW=52,appleBootH=52" in (R/"BootLogoBitmap.h").read_text()

def test_adc_invalid_voltage_is_not_reported_as_charging():
    batt=(R/"Battery.cpp").read_text()
    h=(R/"Battery.h").read_text()
    dbg=(R/"DevConsole.cpp").read_text()
    assert "fault=volts>4.35f?2:(volts<2.90f?1:0);" in batt
    assert "if(fault){" in batt and "chargeDetected=false;" in batt
    assert "chargeDetected && fault==0" in h
    assert "ADC_STATUS=%s" in dbg

def test_adc_decoder_stays_active_but_debug_streaming_is_gone():
    inp=(R/"Input.cpp").read_text()
    dbg=(R/"DevConsole.cpp").read_text()
    ino=(R/"DIY_iPod_Mono.ino").read_text()
    assert "return decodeButtonMillivolts(readingMv);" in inp
    assert "buttonSupplyCompensation.observe(readingMv);" in inp
    assert "input.buttonAdcMillivolts()" not in dbg
    assert "$BTNS_ADC" not in dbg
    assert "$BUTTON_ADC" not in dbg
    assert "ui.event(k,input.lastEventFromWheel())" in ino
