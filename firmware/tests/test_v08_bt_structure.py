from pathlib import Path
R=Path(__file__).resolve().parent.parent

def test_sane_bluetooth_compile_guard():
    for part in ('DIY_iPod_Mono','Wokwi_ILI9341'):
        text=(R/part/'BluetoothOutput.cpp').read_text()
        assert text.strip().endswith('#endif // FEATURE_BLUETOOTH: keep ALL definitions behind the same guard.')
        assert text.index('#if FEATURE_BLUETOOTH')<text.index('bool BluetoothAudioOutput::onGap(')

def test_input_and_wokwi_single_entry():
    for part in ('DIY_iPod_Mono','Wokwi_ILI9341'):
        text=(R/part/'Input.cpp').read_text()
        assert 'millis()-pressed>=1500' in text
        assert 'return Key::SelectLong;' in text
        assert 'if(old==Key::Select && keyStable!=Key::Select && !selectLongSent)' in text
    sim=R/'Wokwi_ILI9341'
    assert (sim/'sketch.ino').exists()
    assert not (sim/'DIY_iPod_Mono.ino').exists()

def test_bluetooth_navigation_and_avrc_queue():
    for part in ('DIY_iPod_Mono','Wokwi_ILI9341'):
        ui=(R/part/'UI.cpp').read_text()
        bt=(R/part/'BluetoothOutput.cpp').read_text()
        assert 'add("Paired Devices",0)' in ui
        assert 'add("Scan and Pair",1)' in ui
        assert 'Key::SelectLong' in ui
        assert 'case ESP_BT_GAP_PIN_REQ_EVT' in bt
        assert 'case ESP_BT_GAP_CFM_REQ_EVT' in bt
        assert 'case ESP_BT_GAP_KEY_REQ_EVT' in bt
        assert 'xQueueSend(remoteQueue,&k,0)' in bt
