"""Runtime dev console integration: USB binary protocol must stay isolated."""
from pathlib import Path
from subprocess import run
import tempfile
ROOT=Path(__file__).resolve().parents[1]

def test_native_protocol_parser():
    with tempfile.TemporaryDirectory() as t:
        binary=Path(t)/'console_test'
        run(['g++','-std=c++17','-Wall','-Wextra','-Werror',str(ROOT/'tests/dev_console_protocol_test.cpp'),'-o',str(binary)],check=True)
        run([str(binary)],check=True)

def test_both_profiles_runtime_console_present():
    for folder,entry in [('DIY_iPod_Mono','DIY_iPod_Mono.ino'),('Wokwi_ILI9341','sketch.ino')]:
        path=ROOT/folder
        ino=(path/entry).read_text()
        assert 'devConsole.loop();' in ino
        assert 'if(!devConsole.hardwareDisabled()){' in ino
        assert 'Key k=input.poll();' in ino
        assert ino.index('if(!devConsole.hardwareDisabled()){') < ino.index('Key k=input.poll();')
        assert 'Key virtualKey=devConsole.pollKey();' in ino
        assert 'ui.event(virtualKey);' in ino
        ui=(path/'UI.cpp').read_text()
        assert 'input.held() && !devConsole.hardwareDisabled()' in ui
        assert 'if(syncMode.active()){' in ui
        code=(path/'DevConsole.cpp').read_text()
        assert 'MONOPOD_LOG.available()' in code
        assert 'Serial.available()' not in code
        assert 'input.resetState();' in code
        assert 'ERR SYNC_LOCKED' in code
        assert 'ERR LINE_TOO_LONG' in code

def test_config_keeps_serial_channels_separate():
    cfg=(ROOT/'DIY_iPod_Mono/Config.h').read_text()
    assert '#define MONOPOD_LOG Serial0' in cfg
    assert '#define MONOPOD_LOG Serial' in cfg
    usb=(ROOT/'DIY_iPod_Mono/UsbSync.cpp').read_text()
    assert 'Serial.available()' in usb
    assert 'MONOPOD_LOG' not in usb
