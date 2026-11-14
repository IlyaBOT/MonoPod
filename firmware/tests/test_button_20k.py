from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]/'DIY_iPod_Mono'
c=(root/'Config.h').read_text()
i=(root/'Input.cpp').read_text()
assert 'PIN_BUTTON_ADC = 9' in c
assert 'BUTTON_ADC_INTERNAL_PULLUP = false;' in c
assert 'pinMode(Config::PIN_BUTTON_ADC, INPUT);' in i
assert 'external 20k pull-up' in c
assert 'External 20k GPIO9 pull-up' in i
windows=re.findall(r'\{\s*(\d+)\s*,\s*(\d+)\s*\}', c.split('BUTTON_ADC_WINDOWS_MV[5][2] = {',1)[1].split('};',1)[0])
windows=[tuple(map(int,w)) for w in windows]
assert len(windows)==5
for i,w in enumerate(windows):
    assert w[0]<=w[1]
    if i: assert windows[i-1][1]<w[0]
for i,range_ in enumerate([(0,0),(654,657),(1064,1085),(1604,1702),(2194,2217)]):
    assert windows[i][0]<=range_[0] and range_[1]<=windows[i][1]
assert not any(lo<=3148<=hi for lo,hi in windows)
print('GPIO9 20k ADC windows OK')
