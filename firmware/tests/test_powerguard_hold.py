from pathlib import Path

root = Path(__file__).resolve().parents[1]
app = (root / 'DIY_iPod_Mono' / 'DIY_iPod_Mono.ino').read_text()
power = (root / 'DIY_iPod_Mono' / 'PowerGuard.cpp').read_text()
config = (root / 'DIY_iPod_Mono' / 'Config.h').read_text()

def test_power_guard_runs_before_full_startup():
    assert app.index('PowerGuard::earlyBoot()') < app.index('settings.begin()')
    assert app.index('PowerGuard::earlyBoot()') < app.index('media.begin(')
    assert app.index('PowerGuard::loop()') > app.index('battery.loop()')


def test_hold_is_rtc_gpio_with_external_pullup():
    assert 'PIN_HOLD = 21;' in config
    assert 'pinMode(Config::PIN_HOLD, INPUT)' in power
    assert 'esp_sleep_enable_ext0_wakeup' in power
    assert 'enterSleep(kWaitLock,true)' in power
    assert 'enterSleep(kWaitUnlock,false)' in power


def test_no_storage_or_music_init_before_power_check():
    boot = power.split('void PowerGuard::earlyBoot()')[1]
    assert 'media.begin' not in boot
    assert 'wifiNetworks.begin' not in boot
    assert 'player.begin' not in boot
    assert 'warningScreen(railMv)' not in boot
    assert 'warnAndSleep(railMv)' in boot


def test_not_sleep_while_sync_write_or_flash_write():
    assert 'wifiSync.busy() || usbSync.busy()' in power
    assert 'settings.saveNow()' not in power


def test_hysteresis_and_battery_window():
    assert 'kShutdownMv = 3050' in power
    assert 'kBootResumeMv = 3120' in power
    assert 'kReleasedMinMv = 2450' in power
    assert 'delay(3000)' in power
    assert 'Connect charger' in power


def test_ota_is_blocked_for_low_rail():
    ota = (root / 'DIY_iPod_Mono' / 'WiFiOta.cpp').read_text()
    assert 'PowerGuard::voltageSafeForOta()' in ota
    assert 'LOW_SUPPLY_OR_BUTTON_HELD' in ota
    assert 'mv>=kBootResumeMv' in power
