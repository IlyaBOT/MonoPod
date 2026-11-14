from pathlib import Path
root=Path(__file__).resolve().parents[1]/'DIY_iPod_Mono'
c=(root/'Config.h').read_text()
i=(root/'Input.cpp').read_text()
h=(root/'ButtonAdcCompensation.h').read_text()
assert 'PIN_BUTTON_ADC = 9' in c
assert 'BUTTON_ADC_INTERNAL_PULLUP = false;' in c
assert 'pinMode(Config::PIN_BUTTON_ADC, INPUT);' in i
assert 'external 20k pull-up to 3V3' in c
assert 'MENU=0, PREV=5.1k, NEXT=10k, PLAY=20k, SELECT=39.9k' in c
assert 'buttonSupplyCompensation.observe(readingMv);' in i
assert 'buttonSupplyCompensation.decode(mV)' in i
assert 'if (adcMv >= kIdleMinMv) return Button::None;' in h
assert 'BUTTON_ADC_WINDOWS_MV' not in c  # fixed windows removed, not just broadened
print('GPIO9 external 20k with adaptive rail ratio: OK')
