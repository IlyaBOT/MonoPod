"""Structural regressions for S3 clicker, SD/TFT SPI and ADC diagnostics."""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]


def source(name, folder='DIY_iPod_Mono'):
    return (ROOT/folder/name).read_text(encoding='utf-8')


def test_hardware_disable_silences_both_click_paths():
    for folder in ('DIY_iPod_Mono', 'Wokwi_ILI9341'):
        dev = source('DevConsole.cpp', folder)
        fb = source('Feedback.cpp', folder)
        assert 'feedback.setRuntimeMuted(disabled || clickerMuteRequested)' in dev
        assert 'void FeedbackController::quiet()' in fb
        assert 'player.stopHeadphoneClick()' in fb
        assert 'if(syncMode.active() || suppressed)return;' in fb
        assert 'ledcWrite(Config::PIN_PIEZO,0)' in fb


def test_mute_persists_until_unmuted():
    for folder in ('DIY_iPod_Mono','Wokwi_ILI9341'):
        dev=source('DevConsole.cpp',folder)
        assert 'clickerMuteRequested=c.muteClicker;' in dev
        assert 'feedback.setRuntimeMuted(clickerMuteRequested || disabled)' in dev
        assert 'case Type::ClickerStatus:' in dev


def test_sd_spi_initialization_order_and_nondestructive_fallback():
    for folder,entry in (('DIY_iPod_Mono','DIY_iPod_Mono.ino'),('Wokwi_ILI9341','sketch.ino')):
        sketch=source(entry,folder)
        assert sketch.index('sdMounted=media.begin()') < sketch.index('display.begin()')
        lib=source('Library.cpp',folder)
        assert '4000000,1000000,400000' in lib
        assert 'SD.end();' in lib
        ui=source('UI.cpp',folder)
        assert '#if FEATURE_SD_FORMAT' in ui
        assert 'Format unavailable' in ui


def test_adc_does_not_invent_calibration_factor():
    for folder in ('DIY_iPod_Mono','Wokwi_ILI9341'):
        bat=source('Battery.cpp',folder)
        assert 'analogSetPinAttenuation(Config::PIN_BATTERY_ADC, ADC_11db)' in bat
        assert '(total/12.0f)*2.0f/1000.0f' in bat
        assert 'delayMicroseconds(120)' in bat
        assert 'RAW=%u' in source('DevConsole.cpp',folder)


def test_standalone_sketch_read_only():
    text=(ROOT/'examples/S3_ADC_SD_NonDestructive_Diagnostic/S3_ADC_SD_NonDestructive_Diagnostic.ino').read_text()
    assert 'SD.begin(' in text
    assert 'analogReadMilliVolts(4)' in text
    assert 'SD.format(' not in text
    assert 'FILE_WRITE' not in text
