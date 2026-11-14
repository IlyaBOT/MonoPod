from pathlib import Path
ROOT = Path(__file__).resolve().parents[1] / 'DIY_iPod_Mono'

def test_header_now_playing_is_full():
    display = (ROOT/'Display.cpp').read_text()
    assert 'compact=(title=="Now Playing"' in display
    assert 'const String fitted=compact?title:fittedTitle(title,94);' in display
    font = (ROOT/'MonoFont.h').read_text()
    import re
    m = re.search(r'static const Glyph small\[\] PROGMEM = \{(.*?)\};', font, re.S)
    advances = {int(c):int(a) for c,a in re.findall(r'\{(\d+),(\d+),\d+,\d+\}',m.group(1))}
    assert sum(advances[ord(c)] for c in 'Now Playing') <= 94

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

def test_fw_version_unchanged_protocol():
    for fname in ('DIY_iPod_Mono.ino','UI.cpp','SyncFiles.cpp','UsbSync.cpp'):
        assert '0.10.2' in (ROOT/fname).read_text()
    assert '"protocol":4' in (ROOT/'SyncFiles.cpp').read_text().replace('\\','')


def test_id3_scans_large_artwork_without_allocating_it():
    p = (ROOT/'Id3Parser.h').read_text()
    assert 'kMaxTagScan=32UL*1024UL*1024UL' in p
    assert 'kMaxFrameHeaders=8192' in p
    assert 'len-payloadOffset<=kMaxTextFrame' in p
    assert 'f.seek(content+payloadOffset)' in p
