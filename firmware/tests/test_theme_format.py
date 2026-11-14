import unittest
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent.parent/'Studio'))
from theme_format import ThemeOptions,find_default_font,encode_theme,verify_theme,demo_preview,CHARS

class ThemeFormatTest(unittest.TestCase):
    def setUp(self):self.opts=ThemeOptions(font_path=find_default_font())
    def test_encode_decode(self):
        raw=encode_theme(self.opts)
        self.assertTrue(28<len(raw)<131100)
        decoded=verify_theme(raw)
        self.assertEqual(decoded['main_glyphs'],len(CHARS))
    def test_crc_fail(self):
        raw=bytearray(encode_theme(self.opts));raw[-20]^=1
        with self.assertRaises(ValueError):verify_theme(raw)
    def test_reject_invalid_oversize(self):
        raw=encode_theme(self.opts)+b'garbage'
        with self.assertRaises(ValueError):verify_theme(raw)
    def test_monochrome_preview(self):
        im=demo_preview(self.opts)
        self.assertEqual(im.size,(160,128))
        self.assertLessEqual(len(im.getcolors(160*128) or []),4)
    def test_antialias_preview(self):
        self.opts.smoothing=True
        im=demo_preview(self.opts)
        self.assertGreater(len(im.getcolors(160*128) or []),4)
    def test_sizes(self):
        self.opts.main_size=19
        self.opts.small_size=14
        verify_theme(encode_theme(self.opts))

if __name__=='__main__':unittest.main()
