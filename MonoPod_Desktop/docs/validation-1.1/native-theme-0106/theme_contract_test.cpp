#include "Theme.h"
#include "SD.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

SDAdapter SD;
static void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static uint16_t u16(const std::vector<uint8_t>& p, size_t n) { return p[n] | uint16_t(p[n + 1]) << 8; }
static uint32_t u32(const std::vector<uint8_t>& p, size_t n) { return p[n] | uint32_t(p[n + 1]) << 8 | uint32_t(p[n + 2]) << 16 | uint32_t(p[n + 3]) << 24; }

int main(int argc, char** argv) {
  try {
    check(argc == 2 || argc == 3, "expected valid fixture and optional invalid replacement");
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    check(raw.size() >= 28, "fixture missing or truncated");
    check(ThemeManager::validate(argv[1]), "actual firmware validate rejected C# fixture");
    check(theme.reload(argv[1]) && theme.active(), "actual firmware reload rejected C# fixture");
    const bool v2 = raw[3] == '2';
    const size_t header = v2 ? 44 : 28;
    for (unsigned i = 0; i < 12; ++i) {
      const uint16_t expected = i < (v2 ? 12u : 4u) ? u16(raw, 12 + i * 2) : u16(raw, i == 4 ? 12 : 14);
      check(theme.color(i) == expected, "RGB565 palette differs from C# fixture");
    }
    size_t pos = header, glyphs = 0, pixels = 0;
    for (unsigned table = 0; table < 2; ++table) {
      const unsigned height = table ? 12 : 16;
      for (unsigned i = 0; i < u16(raw, 8 + table * 2); ++i) {
        const uint32_t cp = u32(raw, pos);
        const uint8_t width = raw[pos + 4], advance = raw[pos + 5];
        pos += 7;
        ThemeManager::Glyph glyph;
        check(theme.glyph(cp, table != 0, glyph), "fixture glyph missing from actual firmware lookup");
        check(glyph.width == width && glyph.advance == advance, "glyph metrics differ from C# fixture");
        for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
          const size_t pixel = y * width + x;
          const uint8_t expected = pixel & 1 ? raw[pos + pixel / 2] & 15 : raw[pos + pixel / 2] >> 4;
          check(theme.alpha(glyph, table != 0, x, y) == expected, "4-bit glyph coverage differs from C# fixture");
          ++pixels;
        }
        pos += (width * height + 1) / 2;
        ++glyphs;
      }
    }
    check(pos == raw.size(), "fixture parser did not consume exact payload");
    ThemeManager::Glyph latin, cyrillic;
    check(theme.glyph('M', false, latin) && theme.glyph(0x416, false, cyrillic), "Latin M or Cyrillic Zh missing");
    const auto firstColor = theme.color(0);
    const auto firstPixel = theme.alpha(latin, false, 0, 0);
    if (argc == 3) {
      check(!ThemeManager::validate(argv[2]), "actual firmware validate accepted invalid replacement");
      check(!theme.reload(argv[2]), "actual firmware reload accepted invalid replacement");
      ThemeManager::Glyph kept;
      check(theme.active() && theme.glyph('M', false, kept), "invalid replacement unloaded valid theme");
      check(kept.width == latin.width && kept.advance == latin.advance && theme.alpha(kept, false, 0, 0) == firstPixel && theme.color(0) == firstColor,
            "invalid replacement changed the active theme");
    }
    std::cout << "PASS actual Theme.cpp 0.10.6: palette, " << glyphs << " glyphs, " << pixels << " alpha pixels";
    if (argc == 3) std::cout << "; invalid replacement rejected, prior theme retained";
    std::cout << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
