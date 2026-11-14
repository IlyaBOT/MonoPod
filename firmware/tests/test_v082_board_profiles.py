"""Static tests for ESP32-S3 compile-time feature stripping, and menu IDs."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROFILES = ('DIY_iPod_Mono', 'Wokwi_ILI9341')


def test_profiles_target_auto_selection_and_force_bt_off():
    for name in PROFILES:
        config = (ROOT / name / 'Config.h').read_text()
        assert '#define MONOPOD_BOARD_ESP32 1' in config
        assert '#define MONOPOD_BOARD_ESP32_S3 2' in config
        assert 'defined(CONFIG_IDF_TARGET_ESP32S3)' in config
        assert '#define FEATURE_BLUETOOTH 0' in config
        assert '#error "ESP32-S3 cannot use Bluetooth Classic/A2DP.' in config
        assert '#define MONOPOD_HAS_BT_CLASSIC 0' in config


def test_s3_uses_valid_touch_pins_and_new_spi_and_reset():
    for name in PROFILES:
        config = (ROOT / name / 'Config.h').read_text()
        s3 = config.split('#if MONOPOD_BOARD == MONOPOD_BOARD_ESP32_S3\nconstexpr uint8_t PIN_SPI_SCK')[1].split('#else\n// Original ESP32')[0]
        assert 'PIN_TFT_RST = 47;' in s3
        assert 'PIN_BUTTON_ADC = 9;' in s3
        assert 'WHEEL_PINS[6] = {1,2,5,6,7,8};' in s3
        for forbidden in ('PIN_TFT_CS = 38;', 'PIN_TFT_CS = 48;', 'PIN_BATTERY_ADC = 36;'):
            assert forbidden not in s3
        inp = (ROOT / name / 'Input.cpp').read_text()
        assert 'amplitude=(float)raw[i]-(float)base[i];' in inp
        assert 'amplitude=(float)base[i]-(float)raw[i];' in inp
        assert 'uint64_t accum[6]' in inp


def test_bluetooth_menu_and_global_state_are_guarded():
    for name in PROFILES:
        ui = (ROOT / name / 'UI.cpp').read_text()
        assert '#if FEATURE_BLUETOOTH\n    add("Bluetooth",8);\n#endif' in ui
        assert 'switch(items[selected].id)' in ui
        assert '#if FEATURE_BLUETOOTH\n    case 8:navigate(Page::Bluetooth);return;\n#endif' in ui
        header = (ROOT / name / 'UI.h').read_text()
        assert '#if FEATURE_BLUETOOTH\n  uint32_t lastNetworkUiUpdate' in header
        eng = (ROOT / name / 'AudioEngine.h').read_text()
        assert '#if FEATURE_BLUETOOTH\n  bool bluetooth()' in eng
