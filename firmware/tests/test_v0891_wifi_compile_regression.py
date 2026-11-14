"""Arduino integration regressions from v0.8.9: missing include and NVS accessor.

The native header test uses a tiny Arduino String shim. This checks the C++
interface, NOT a full Arduino-ESP32 firmware build.
"""
from pathlib import Path
from subprocess import run
import shutil
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def test_sketches_include_wifi_before_uses_and_expose_accessor():
    for folder, entry in (("DIY_iPod_Mono", "DIY_iPod_Mono.ino"), ("Wokwi_ILI9341", "sketch.ino")):
        src = (ROOT / folder / entry).read_text(encoding="utf-8")
        assert src.count('#include "WiFiNetworks.h"') == 1
        assert src.index('#include "WiFiNetworks.h"') < src.index('wifiNetworks.begin();')
        header = (ROOT / folder / "WiFiNetworks.h").read_text(encoding="utf-8")
        assert "bool hasSavedNetwork() const { return !savedSSID.isEmpty(); }" in header
        assert "extern WiFiNetworkManager wifiNetworks;" in header
        console = (ROOT / folder / "DevConsole.cpp").read_text(encoding="utf-8")
        assert '#include "WiFiNetworks.h"' in console
        assert "wifiNetworks.hasSavedNetwork()" in console


def test_public_wifi_header_is_valid_cpp():
    compiler = shutil.which('g++')
    if not compiler:
        raise RuntimeError('g++ is required for the native WiFiNetworks.h C++ smoke test')
    for folder in ("DIY_iPod_Mono", "Wokwi_ILI9341"):
        with tempfile.TemporaryDirectory(prefix='mp0891_') as tmp:
            tmpdir = Path(tmp)
            (tmpdir / "Arduino.h").write_text('''
#pragma once
#include <cstdint>
#include <cstddef>
class String {
public:
  String(const char* = "") {}
  bool isEmpty() const { return true; }
};
''', encoding='utf-8')
            (tmpdir / "test.cpp").write_text('''
#include <type_traits>
#include <utility>
#include "WiFiNetworks.h"
static_assert(std::is_same_v<
  decltype(std::declval<const WiFiNetworkManager&>().hasSavedNetwork()), bool>);
void compile_smoke_test() {
  (void) wifiNetworks.hasSavedNetwork();
}
''', encoding='utf-8')
            run([
                compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsyntax-only',
                '-DCONFIG_IDF_TARGET_ESP32S3=1', '-DARDUINO_ARCH_ESP32=1',
                '-DARDUINO_USB_CDC_ON_BOOT=1',
                '-I', str(tmpdir), '-I', str(ROOT / folder), str(tmpdir / 'test.cpp')
            ], check=True)
