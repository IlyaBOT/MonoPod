from pathlib import Path

P = Path(__file__).resolve().parents[1] / "DIY_iPod_Mono"

def test_clock_targets_and_transaction_isolation():
    cfg = (P / "Config.h").read_text()
    display = (P / "Display.cpp").read_text()
    power = (P / "PowerGuard.cpp").read_text()
    lib = (P / "Library.cpp").read_text()
    assert '#define MONOPOD_SPI_SD_TARGET_HZ 16000000UL' in cfg
    assert '#define MONOPOD_SPI_TFT_TARGET_HZ 20000000UL' in cfg
    assert 'tft.setSPISpeed(Config::SPI_TFT_HZ);' in display
    assert 'tft.setSPISpeed(Config::SPI_TFT_HZ);' in power
    assert 'SD.begin(Config::PIN_SD_CS, SPI, hz)' in lib
    assert 'verifySdClockReadOnly()' in lib
    assert 'SD.end(); // essential' in lib
    assert 'SPI.setFrequency' not in lib

def test_both_chip_selects_high_before_mount():
    lib = (P / 'Library.cpp').read_text()
    assert 'digitalWrite(Config::PIN_TFT_CS,HIGH);' in lib
    assert 'digitalWrite(Config::PIN_SD_CS,HIGH);' in lib
