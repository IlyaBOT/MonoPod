from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def test_single_mount_and_no_probe_loop():
    for sub in ('DIY_iPod_Mono','Wokwi_ILI9341'):
        lib=(ROOT/sub/'Library.cpp').read_text()
        cfg=(ROOT/sub/'Config.h').read_text()
        assert lib.count('SD.begin(Config::PIN_SD_CS,SPI,Config::SPI_SD_HZ)') == 1
        assert 'const uint32_t rates[]' not in lib
        assert 'for(const uint32_t hz:rates)' not in lib
        assert 'constexpr uint32_t SPI_SD_HZ = 4000000;' in cfg
        assert lib.index('digitalWrite(Config::PIN_TFT_CS,HIGH);') < lib.index('SD.begin(')

def test_cleanup_keeps_real_battery_monitoring():
    for sub in ('DIY_iPod_Mono','Wokwi_ILI9341'):
        bat=(ROOT/sub/'Battery.cpp').read_text()
        dev=(ROOT/sub/'DevConsole.cpp').read_text()
        parser=(ROOT/sub/'DevConsoleProtocol.h').read_text()
        assert 'analogReadMilliVolts(Config::PIN_BATTERY_ADC)' in bat
        assert '(total/12.0f)*2.0f/1000.0f' in bat
        assert 'case Type::Battery:' in dev
        assert 'case Type::Sd:' in dev
        for obsolete in ('ADC_DIAG','SD_RETRY','Type::AdcDiag','Type::SdRetry'):
            assert obsolete not in dev
            assert obsolete not in parser
