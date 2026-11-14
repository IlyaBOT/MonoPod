from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def test_clicker_mute_is_independent_from_hardware_disable():
    for sub in ('DIY_iPod_Mono','Wokwi_ILI9341'):
        src=(ROOT/sub/'DevConsole.cpp').read_text()
        assert 'feedback.setRuntimeMuted(clickerMuteRequested);' in src
        assert 'feedback.setRuntimeMuted(disabled || clickerMuteRequested)' not in src
        assert 'feedback.setRuntimeMuted(clickerMuteRequested || disabled)' not in src
        assert 'feedback.quiet();' in src
        assert 'case Type::AdcDiag:' in src
        assert 'analogSetPinAttenuation(Config::PIN_BATTERY_ADC,ADC_11db);' in src

def test_console_synonyms_and_adc_diagnostic():
    for sub in ('DIY_iPod_Mono','Wokwi_ILI9341'):
        src=(ROOT/sub/'DevConsoleProtocol.h').read_text()
        assert '"CLICKER=1"' in src
        assert '"CLICKER=0"' in src
        assert '"ADC_DIAG"' in src
