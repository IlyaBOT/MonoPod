/*
 * MonoPod UI Demo - ESP8266 + ST7735S 1.8" (160x128 landscape)
 * Arduino IDE, ESP8266 core, Adafruit GFX + Adafruit ST7735/ST7789
 *
 * Hardware SPI: SCK=D5 (GPIO14), MOSI=D7 (GPIO13)
 * CS=D2 (GPIO4), DC=D1 (GPIO5), RST=D0 (GPIO16)
 * Backlight=3V3 (always ON), VCC=3V3, GND=GND
 *
 * DISPLAY-ONLY DEMO: no Wi-Fi/Bluetooth/audio/SD hardware is used.
 * Each screen changes automatically every 5000 milliseconds.
 */

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSans9pt7b.h>

static constexpr uint8_t PIN_TFT_CS  = 4;   // D2
static constexpr uint8_t PIN_TFT_DC  = 5;   // D1
static constexpr uint8_t PIN_TFT_RST = 16;  // D0
static constexpr uint32_t PAGE_MS   = 5000;
static constexpr uint16_t SCREEN_W  = 160;
static constexpr uint16_t SCREEN_H  = 128;

Adafruit_ST7735 tft(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

uint16_t C_BG, C_FG, C_SELECTED, C_SEL_TEXT, C_ACCENT, C_LINE;

enum Page : uint8_t {
  PAGE_HOME,
  PAGE_MUSIC,
  PAGE_TRACKS,
  PAGE_NOW_PLAYING,
  PAGE_SETTINGS,
  PAGE_WIFI,
  PAGE_BLUETOOTH,
  PAGE_STORAGE,
  PAGE_EXTRAS,
  PAGE_ABOUT,
  PAGE_COUNT
};

uint8_t page = PAGE_HOME;
uint32_t pageStartedAt = 0;
uint32_t lastDynamicUpdate = 0;
bool scanResultsShown = false;

// ---------------- Drawing helpers ----------------
void textAt(int16_t x, int16_t baseline, const char *str,
            uint16_t color, bool bold = true) {
  tft.setFont(bold ? &FreeSansBold9pt7b : &FreeSans9pt7b);
  tft.setTextSize(1);
  tft.setTextColor(color);
  tft.setCursor(x, baseline);
  tft.print(str);
}

void smallText(int16_t x, int16_t y, const char *str, uint16_t color) {
  tft.setFont(nullptr);      // Built-in 5x7 font, no antialiasing
  tft.setTextSize(1);
  tft.setTextColor(color);
  tft.setCursor(x, y);
  tft.print(str);
}

void centerText(int16_t baseline, const char *str, bool bold = true) {
  tft.setFont(bold ? &FreeSansBold9pt7b : &FreeSans9pt7b);
  tft.setTextSize(1);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(str, 0, baseline, &x1, &y1, &w, &h);
  textAt((SCREEN_W - static_cast<int16_t>(w)) / 2 - x1,
         baseline, str, C_FG, bold);
}

void batteryIcon(uint8_t percent) {
  const int x = 137, y = 5;
  tft.drawRect(x, y, 18, 9, C_FG);
  tft.fillRect(x + 18, y + 3, 2, 3, C_FG);
  tft.fillRect(x + 2, y + 2, 14, 5, C_BG);
  const uint8_t fill = (static_cast<uint16_t>(percent) * 14) / 100;
  if (fill) tft.fillRect(x + 2, y + 2, fill, 5, C_FG);
}

void header(const char *title) {
  tft.fillScreen(C_BG);
  textAt(4, 15, title, C_FG);
  batteryIcon(76);  // Demo value, not an ADC measurement
  tft.drawFastHLine(0, 19, SCREEN_W, C_FG);
}

void arrowAt(int x, int centerY, uint16_t color) {
  tft.fillTriangle(x, centerY - 5, x, centerY + 5,
                   x + 6, centerY, color);
}

void listRow(uint8_t index, const char *label, bool selected, bool arrow = true) {
  const int y = 21 + index * 20;
  if (selected) tft.fillRect(2, y, 156, 19, C_SELECTED);
  uint16_t ink = selected ? C_SEL_TEXT : C_FG;
  textAt(7, y + 15, label, ink);
  if (arrow) arrowAt(148, y + 10, ink);
}

void listScreen(const char *title, const char *const* entries,
                uint8_t count, uint8_t selected, bool arrows = true) {
  header(title);
  if (count > 5) count = 5;
  for (uint8_t i = 0; i < count; ++i) {
    listRow(i, entries[i], i == selected, arrows);
  }
}

void progressBar(int x, int y, int w, int h, uint16_t value, uint16_t maxValue) {
  tft.drawRect(x, y, w, h, C_FG);
  tft.fillRect(x + 2, y + 2, w - 4, h - 4, C_BG);
  if (maxValue == 0) return;
  int filled = (static_cast<uint32_t>(value) * (w - 4)) / maxValue;
  if (filled > 0) tft.fillRect(x + 2, y + 2, filled, h - 4, C_FG);
}

void formatTime(uint32_t seconds, char *buf, size_t len) {
  snprintf(buf, len, "%lu:%02lu",
           static_cast<unsigned long>(seconds / 60),
           static_cast<unsigned long>(seconds % 60));
}

// ---------------- Demo screens ----------------
void renderHome() {
  static const char *const entries[] = {
    "Music", "Extras", "Settings", "Shuffle Songs", "Backlight"
  };
  listScreen("iPod", entries, 5, 0, false);
  arrowAt(148, 31, C_SEL_TEXT);
  arrowAt(148, 51, C_FG);
  arrowAt(148, 71, C_FG);
}

void renderMusic() {
  static const char *const entries[] = {
    "Playlists", "Artists", "Albums", "Songs", "Genres"
  };
  listScreen("Music", entries, 5, 3);
}

void renderTracks() {
  static const char *const entries[] = {
    "One More Time", "Aerodynamic", "Digital Love", "Harder Better", "Voyager"
  };
  listScreen("Songs", entries, 5, 0, false);
}

// Track is simulated to play across ALL screens, not only Now Playing.
constexpr uint32_t TRACK_SECONDS = 195;
void updateNowPlaying(uint32_t now) {
  uint32_t sec = ((now / 1000UL) % TRACK_SECONDS);
  char current[12], duration[12];
  formatTime(sec, current, sizeof(current));
  formatTime(TRACK_SECONDS, duration, sizeof(duration));
  tft.fillRect(7, 92, 146, 30, C_BG);
  progressBar(10, 94, 140, 9, sec, TRACK_SECONDS);
  smallText(10, 109, current, C_FG);
  smallText(127, 109, duration, C_FG);
  // Minimal playback marker
  tft.fillTriangle(74, 117, 74, 125, 81, 121, C_FG);
}

void renderNowPlaying(uint32_t now) {
  header("Now Playing");
  centerText(45, "One More Time");
  centerText(68, "Daft Punk", false);
  smallText(53, 76, "Discovery", C_FG);
  updateNowPlaying(now);
  lastDynamicUpdate = now;
}

void renderSettings() {
  static const char *const entries[] = {
    "Shuffle", "Repeat", "Clicker", "Backlight", "Font AA"
  };
  listScreen("Settings", entries, 5, 2, false);
  smallText(128, 27, "Off", C_FG);
  smallText(128, 47, "All", C_FG);
  smallText(105, 67, "Speaker", C_SEL_TEXT);
  smallText(128, 87, "75%", C_FG);
  smallText(128, 107, "Off", C_FG);
}

void signalBars(int x, int y, uint8_t quality, uint16_t ink) {
  for (uint8_t i = 0; i < 4; i++) {
    const int h = 3 + i * 3;
    if (i < quality) tft.fillRect(x + i * 5, y + 13 - h, 3, h, ink);
    else tft.drawRect(x + i * 5, y + 13 - h, 3, h, ink);
  }
}

void renderNetworks() {
  header("Wi-Fi");
  const char *names[] = { "Home-WiFi", "MonoPod", "Office-2G", "Guest" };
  const uint8_t signal[] = {4, 3, 2, 1};
  for (uint8_t i = 0; i < 4; ++i) {
    const int y = 24 + i * 24;
    if (i == 0) tft.fillRect(2, y - 2, 156, 21, C_SELECTED);
    textAt(7, y + 14, names[i], i == 0 ? C_SEL_TEXT : C_FG, false);
    signalBars(133, y + 2, signal[i], i == 0 ? C_SEL_TEXT : C_FG);
  }
}

void renderWiFi() {
  header("Wi-Fi");
  centerText(71, "Scanning...");
  smallText(42, 100, "Demo scan", C_FG);
  scanResultsShown = false;
}

void renderBluetoothDevices() {
  static const char *const devices[] = {
    "Headphones", "BT Speaker", "Search Again", "Bluetooth: On"
  };
  listScreen("Bluetooth", devices, 4, 0, false);
  smallText(133, 30, "*", C_SEL_TEXT);
}

void renderBluetooth() {
  header("Bluetooth");
  centerText(71, "Searching...");
  smallText(36, 100, "A2DP demo", C_FG);
  scanResultsShown = false;
}

void sdCardShape(int x, int y) {
  tft.drawRoundRect(x, y + 4, 28, 34, 2, C_FG);
  tft.fillTriangle(x + 18, y + 4, x + 28, y + 4,
                   x + 28, y + 14, C_BG);
  tft.drawLine(x + 18, y + 4, x + 28, y + 14, C_FG);
  for (int i = 0; i < 4; ++i) tft.fillRect(x + 4 + 5*i, y + 11, 3, 7, C_FG);
  smallText(x + 7, y + 25, "SD", C_FG);
}

void renderStorage() {
  header("Storage");
  sdCardShape(10, 28);
  textAt(47, 50, "microSD", C_FG);
  smallText(49, 58, "32.0 GB (demo)", C_FG);
  progressBar(11, 77, 138, 12, 60, 100);
  smallText(11, 97, "Used: 19.2 GB", C_FG);
  smallText(11, 112, "Free: 12.8 GB", C_FG);
}

void renderExtras() {
  static const char *const entries[] = {
    "Clock", "Games", "Contacts", "Notes", "About"
  };
  listScreen("Extras", entries, 5, 0);
}

void renderAbout() {
  header("About");
  textAt(8, 43, "MonoPod Demo", C_FG);
  smallText(9, 58, "ESP8266 / ST7735S", C_FG);
  smallText(9, 72, "LCD: 160 x 128", C_FG);
  smallText(9, 86, "No audio / no SD", C_FG);
  smallText(9, 100, "No wireless connect", C_FG);
  smallText(9, 116, "Screen cycle: 5 sec", C_FG);
}

void renderPage(uint32_t now) {
  switch (page) {
    case PAGE_HOME:        renderHome(); break;
    case PAGE_MUSIC:       renderMusic(); break;
    case PAGE_TRACKS:      renderTracks(); break;
    case PAGE_NOW_PLAYING: renderNowPlaying(now); break;
    case PAGE_SETTINGS:    renderSettings(); break;
    case PAGE_WIFI:        renderWiFi(); break;
    case PAGE_BLUETOOTH:   renderBluetooth(); break;
    case PAGE_STORAGE:     renderStorage(); break;
    case PAGE_EXTRAS:      renderExtras(); break;
    case PAGE_ABOUT:       renderAbout(); break;
    default:               renderHome(); break;
  }
  Serial.printf("[MonoPod demo] Screen %u/%u\n", page + 1, PAGE_COUNT);
}

void setup() {
  Serial.begin(115200);
  SPI.begin();  // ESP8266 HSPI SCK=GPIO14/D5, MOSI=GPIO13/D7
  tft.initR(INITR_BLACKTAB);  // Try INITR_GREENTAB if image is offset
  tft.setRotation(1);         // landscape 160 x 128
  tft.invertDisplay(false);   // set true if your module uses inverse colors
  tft.setTextWrap(false);
  tft.setSPISpeed(16000000);  // use 8000000 if long jumper wires cause glitches

  C_BG       = tft.color565(207, 217, 205);
  C_FG       = tft.color565(28, 42, 31);
  C_SELECTED = tft.color565(31, 54, 39);
  C_SEL_TEXT = tft.color565(230, 238, 227);
  C_ACCENT   = tft.color565(65, 85, 68);
  C_LINE     = tft.color565(75, 91, 77);

  pageStartedAt = millis();
  renderPage(pageStartedAt);
}

void loop() {
  const uint32_t now = millis();
  if (static_cast<uint32_t>(now - pageStartedAt) >= PAGE_MS) {
    pageStartedAt += PAGE_MS;
    page = (page + 1) % PAGE_COUNT;
    renderPage(now);
  }

  if (page == PAGE_NOW_PLAYING && now - lastDynamicUpdate >= 200) {
    updateNowPlaying(now);
    lastDynamicUpdate = now;
  }
  if (page == PAGE_WIFI && !scanResultsShown && now - pageStartedAt >= 1500) {
    renderNetworks();
    scanResultsShown = true;
  }
  if (page == PAGE_BLUETOOTH && !scanResultsShown && now - pageStartedAt >= 1500) {
    renderBluetoothDevices();
    scanResultsShown = true;
  }
  yield();
}
