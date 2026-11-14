from pathlib import Path
ROOT = Path(__file__).resolve().parents[1] / 'DIY_iPod_Mono'

def test_header_now_playing_is_full():
    display = (ROOT/'Display.cpp').read_text()
    assert 'headerTitleMarquee(title,0)' in display
    assert 'windowText(left,small?4:3,width,label,small,scrollPx' in display
    font = (ROOT/'MonoFont.h').read_text()
    import re
    m = re.search(r'static const Glyph small\[\] PROGMEM = \{(.*?)\};', font, re.S)
    advances = {int(c):int(a) for c,a in re.findall(r'\{(\d+),(\d+),\d+,\d+\}',m.group(1))}
    assert sum(advances[ord(c)] for c in 'Now Playing') <= 100

def test_wifi_one_attempt_only():
    s = (ROOT/'WiFiNetworks.cpp').read_text()
    assert 'kConnectTimeoutMs=10000' in s
    assert 'WiFi.setAutoReconnect(false);' in s
    assert 'nextRetryAt' not in s
    assert s.count('beginConnection(savedSSID,savedPassword,false)') == 1 # startup only
    assert 'WiFi.disconnect(false);' in s

def test_id3_uses_new_decoder():
    s = (ROOT/'Library.cpp').read_text()
    assert 'Id3Parser::parse(f,tags)' in s
    assert 'static String id3Text(' not in s
    p = (ROOT/'Id3Parser.h').read_text()
    assert 'enc==1 || enc==2' in p
    assert 'kMaxTextFrame=4096' in p
    assert 'new(std::nothrow)' in p

def test_fw_version_updated_protocol_unchanged():
    for fname in ('DIY_iPod_Mono.ino','UI.cpp','SyncFiles.cpp'):
        assert '0.10.7' in (ROOT/fname).read_text()
    assert '"protocol":4' in (ROOT/'SyncFiles.cpp').read_text().replace('\\','')


def test_id3_scans_large_artwork_without_allocating_it():
    p = (ROOT/'Id3Parser.h').read_text()
    assert 'kMaxTagScan=32UL*1024UL*1024UL' in p
    assert 'kMaxFrameHeaders=8192' in p
    assert 'len-payloadOffset<=kMaxTextFrame' in p
    assert 'f.seek(content+payloadOffset)' in p

def test_marquee_pixel_scrolling_and_clipping():
    display = (ROOT/'Display.cpp').read_text()
    ui = (ROOT/'UI.cpp').read_text()
    assert 'void Display::windowText(' in display
    assert 'dx<clipLeft||dx>=clipRight' in display
    assert 'windowText(4,38,152,title,true' in display
    assert 'surface().startWrite();' in display
    assert 'MarqueeMotion::pixelOffset' in ui
    assert 'MarqueeMotion::phase' in display
    assert 'display.listRow((uint8_t)row,label,true,submenu,(int)frame)' in ui

def test_offscreen_backlight_stays_off():
    ui = (ROOT/'UI.cpp').read_text()
    assert 'now-lastHiddenRefresh>=600000UL' in ui
    assert 'lastHiddenTrack=track;' in ui
    block = ui.split('if(blank && !syncMode.active()){')[1].split('// Marquee is updated')[0]
    assert 'backlight.wake()' not in block

def test_click_and_default_metadata():
    feedback = (ROOT/'Feedback.cpp').read_text()
    library = (ROOT/'Library.cpp').read_text()
    assert 'startTone(Config::PIEZO_SCROLL_HZ,3)' in feedback
    assert 'headphoneClick(Config::PIEZO_SCROLL_HZ,2)' in feedback
    for v in ('Unknown Artist','Unknown Album','Unknown Genre'):
        assert v in library
    assert 'if(tags.year)t.year=tags.year;' in library
