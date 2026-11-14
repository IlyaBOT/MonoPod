// MonoPod virtual capacitive Click Wheel for Wokwi Custom Chips API.
// Wokwi has no custom pointer/mouse-drag events. Angle is controlled by a range slider.
// Outputs E0..E5 are DIGITAL proxies of 6 touch pads, NOT physical capacitance.
#include "wokwi-api.h"
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define W 180
#define H 180
#define PI_F 3.14159265358979323846f

static struct {
  pin_t pads[6];
  uint32_t angle_attr;
  uint32_t touch_attr;
  buffer_t display;
  timer_t tick;
  int previous_angle;
  int previous_touch;
} chip;

static uint8_t pixels[W * H * 4];

static void pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
  if ((unsigned)x >= W || (unsigned)y >= H) return;
  unsigned p = ((unsigned)y * W + (unsigned)x) * 4u;
  pixels[p + 0] = r;
  pixels[p + 1] = g;
  pixels[p + 2] = b;
  pixels[p + 3] = 255;
}

static void rect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
  for (int yy = y; yy < y + h; ++yy) {
    for (int xx = x; xx < x + w; ++xx) pixel(xx, yy, r, g, b);
  }
}

// Compact 3x5 pixel glyphs for MENU, playback and transport labels.
static const uint8_t glyphs[][5] = {
  {7,5,5,5,7}, // O  0
  {5,7,7,7,5}, // M  1
  {7,4,6,4,7}, // E  2
  {5,7,7,7,5}, // N  3  (block lettering)
  {5,5,5,5,7}, // U  4
  {7,4,7,1,7}, // S  5
  {7,2,2,2,2}, // T  6
  {2,5,7,5,5}, // A  7
  {6,5,6,5,5}, // R  8
  {4,4,4,4,7}, // L  9
  {5,5,5,5,2}, // V 10
  {5,5,7,5,5}, // H 11
  {0,0,7,0,0}, // - 12
  {0,0,0,0,0}, // space 13
  {4,2,1,2,4}, // < 14
  {1,2,4,2,1}, // > 15
  {2,2,2,0,2}  // ! 16
};

static int glyph_index(char c) {
  switch (c) {
    case 'O': return 0; case 'M': return 1; case 'E': return 2;
    case 'N': return 3; case 'U': return 4; case 'S': return 5;
    case 'T': return 6; case 'A': return 7; case 'R': return 8;
    case 'L': return 9; case 'V': return 10; case 'H': return 11;
    case '-': return 12; case '<': return 14; case '>': return 15;
    default: return 13;
  }
}

static void txt(int x, int y, const char *str, uint8_t r, uint8_t g, uint8_t b, int scale) {
  while (*str) {
    const uint8_t *bits = glyphs[glyph_index(*str++)];
    for (int yy = 0; yy < 5; ++yy)
      for (int xx = 0; xx < 3; ++xx)
        if (bits[yy] & (1u << (2 - xx)))
          rect(x + xx * scale, y + yy * scale, scale, scale, r, g, b);
    x += 4 * scale;
  }
}

// All angles: clockwise from twelve o'clock. Wrap safely through 359 -> 0.
static int circular_delta(int a, int b) {
  int d = (a - b + 540) % 360 - 180;
  return d;
}

static void draw_wheel(int angle, int touching) {
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      const float dx = (float)(x - W / 2);
      const float dy = (float)(y - H / 2);
      const float radius2 = dx * dx + dy * dy;
      if (radius2 > 80.0f * 80.0f) {
        pixel(x, y, 37, 40, 46);
      } else if (radius2 >= 77.5f * 77.5f) {
        pixel(x, y, 106, 109, 113);
      } else if (radius2 > 36.0f * 36.0f) {
        float a = atan2f(dx, -dy) * (180.0f / PI_F);
        if (a < 0) a += 360.0f;
        const int sector = ((int)(a + 30.0f) / 60) % 6;
        const float boundary = fabsf((float)circular_delta((int)a, sector * 60));
        const int active = touching && (fabsf((float)circular_delta(angle, sector * 60)) <= 34.0f);
        if (boundary >= 28.5f) {
          pixel(x, y, 162, 167, 171); // physical gap between electrodes
        } else if (active) {
          pixel(x, y, 160, 192, 209);
        } else if (sector & 1) {
          pixel(x, y, 228, 231, 234);
        } else {
          pixel(x, y, 240, 241, 242);
        }
      } else if (radius2 >= 33.0f * 33.0f) {
        pixel(x, y, 159, 164, 170);
      } else {
        pixel(x, y, 224, 226, 229);
      }
    }
  }
  // iPod-inspired labels: placement is approximate, not copied source art.
  txt(74, 20, "MENU", 30, 35, 40, 2);
  txt(25, 86, "<<", 30, 35, 40, 2);
  txt(132, 86, ">>", 30, 35, 40, 2);
  txt(74, 148, "PLAY", 30, 35, 40, 2);
  txt(69, 85, "SELECT", 55, 58, 62, 1);
  if (touching) {
    const float radians = angle * PI_F / 180.0f;
    const int px = 90 + (int)(sinf(radians) * 57.0f);
    const int py = 90 - (int)(cosf(radians) * 57.0f);
    for (int yy = -4; yy <= 4; ++yy)
      for (int xx = -4; xx <= 4; ++xx)
        if (xx * xx + yy * yy <= 16) pixel(px + xx, py + yy, 54, 100, 132);
  }
  buffer_write(chip.display, 0, pixels, sizeof(pixels));
}

static void update(void *userdata) {
  (void)userdata;
  const int angle = (int)(attr_read(chip.angle_attr) % 360);
  const int touching = attr_read(chip.touch_attr) != 0;
  if (angle == chip.previous_angle && touching == chip.previous_touch) return;
  chip.previous_angle = angle;
  chip.previous_touch = touching;

  // A real ESP32 reads *capacitance*, not HIGH/LOW. These outputs are
  // intentionally digital so Wokwi can test the menu's angle decoder.
  for (int i = 0; i < 6; ++i) {
    const bool pressed = touching && fabsf((float)circular_delta(angle, i * 60)) <= 34.0f;
    pin_write(chip.pads[i], pressed ? HIGH : LOW);
  }
  draw_wheel(angle, touching);
}

void chip_init(void) {
  static const char *const names[6] = {"E0", "E1", "E2", "E3", "E4", "E5"};
  for (int i = 0; i < 6; ++i) chip.pads[i] = pin_init(names[i], OUTPUT_LOW);
  chip.angle_attr = attr_init("angle", 0);
  chip.touch_attr = attr_init("touch", 1);
  uint32_t width, height;
  chip.display = framebuffer_init(&width, &height);
  chip.previous_angle = -1;
  chip.previous_touch = -1;
  const timer_config_t config = {
    .callback = update,
    .user_data = NULL,
  };
  chip.tick = timer_init(&config);
  timer_start(chip.tick, 12000, true); // poll Wokwi sliders every 12ms
  update(NULL);
}
