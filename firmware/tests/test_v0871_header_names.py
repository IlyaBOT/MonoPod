"""Regression: Arduino-ESP32 3.3.x system limits.h defines LINE_MAX macro."""
from pathlib import Path
from subprocess import run
import tempfile
ROOT=Path(__file__).resolve().parents[1]

def test_console_capacity_cannot_collide_with_limits_h():
    for folder in ('DIY_iPod_Mono', 'Wokwi_ILI9341'):
        hdr=(ROOT/folder/'DevConsole.h').read_text()
        cpp=(ROOT/folder/'DevConsole.cpp').read_text()
        assert 'CONSOLE_LINE_CAPACITY' in hdr
        assert 'CONSOLE_LINE_CAPACITY' in cpp
        assert 'LINE_MAX' not in hdr+cpp
    with tempfile.TemporaryDirectory() as d:
        src=Path(d)/'test.cpp'
        src.write_text('''#include <limits.h>
#ifndef LINE_MAX
#define LINE_MAX 2048
#endif
#include "DevConsole.h"
int main(){ DevelopmentConsole console; return console.hardwareDisabled() ? 1 : 0; }
''')
        run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
             '-DCONFIG_IDF_TARGET_ESP32S3=1', '-DARDUINO_USB_CDC_ON_BOOT=1',
             '-I', str(ROOT/'tests/native_stub'), '-I', str(ROOT/'DIY_iPod_Mono'),
             '-fsyntax-only', str(src)], check=True)
