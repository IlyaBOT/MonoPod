/*
  ESP8266 -> PCM5102A -> TDA1308: sine wave signal path diagnostic.

  Plays five actual 3-second 48 kHz / 16-bit mono PCM WAV files from LittleFS:
    500 Hz -> 1000 Hz -> 5000 Hz -> 10000 Hz -> 20000 Hz -> repeat.

  Requirements:
  - NodeMCU or Wemos D1 mini ESP8266 with >= 4 MB flash
  - ESP8266Audio by Earle F. Philhower III
  - ESP8266 Arduino core with LittleFS support
  - Flash layout with >= 2 MB LittleFS partition
  - Upload all five WAV files from the data/ folder into LittleFS

  ESP8266 I2S TX FIXED pins (identical to previous ESP8266_AudioTest.ino):
    GPIO15 / D8 -> PCM5102A BCK
    GPIO2  / D4 -> PCM5102A LCK / LRCK
    GPIO3  / RX -> PCM5102A DIN
    GND         -> PCM5102A GND
    3V3         -> PCM5102A VCC (ONLY for suitable 3.3 V breakout)

  WARNING: GPIO3 / RX might be driven by onboard USB-to-UART TX.
  If you observe corrupted I2S, check for electrical contention.
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include "AudioFileSourceLittleFS.h"
#include "AudioGeneratorWAV.h"
#include "AudioOutputI2S.h"

// ================= USER SETTINGS ==================
// Digital gain applied to each DAC channel: 0.0 = silence, 1.0 = unity.
// Start low: PCM5102A can reach approximately 2.1 Vrms at full scale.
static constexpr float OUTPUT_GAIN = 0.20f;
// ==================================================

static const char *const TRACKS[] = {
  "/tone_500.wav",
  "/tone_1000.wav",
  "/tone_5000.wav",
  "/tone_10000.wav",
  "/tone_20000.wav"
};
static const uint32_t TRACK_HZ[] = {500, 1000, 5000, 10000, 20000};
static constexpr size_t TRACK_COUNT = sizeof(TRACKS) / sizeof(TRACKS[0]);
static constexpr uint32_t WAV_BYTES = 44 + 48000UL * 3UL * 2UL;

AudioOutputI2S dac;
AudioFileSourceLittleFS *source = nullptr;
AudioGeneratorWAV *player = nullptr;
size_t trackIndex = 0;
bool ready = false;

void closeTrack() {
  if (player != nullptr) {
    if (player->isRunning()) {
      player->stop();
    }
    delete player;
    player = nullptr;
  }
  if (source != nullptr) {
    source->close();
    delete source;
    source = nullptr;
  }
}

bool playTrack(size_t index) {
  closeTrack();
  source = new AudioFileSourceLittleFS(TRACKS[index]);
  if (source == nullptr || !source->isOpen()) {
    Serial.printf("ERROR: Cannot open %s\n", TRACKS[index]);
    closeTrack();
    return false;
  }

  player = new AudioGeneratorWAV();
  if (player == nullptr) {
    Serial.println(F("ERROR: No memory for WAV decoder"));
    closeTrack();
    return false;
  }

  dac.SetGain(OUTPUT_GAIN);
  // WAV decoder selects 48 kHz and mono, then duplicates mono to both DAC channels.
  if (!player->begin(source, &dac)) {
    Serial.printf("ERROR: Cannot start WAV %s\n", TRACKS[index]);
    closeTrack();
    return false;
  }

  Serial.printf("Playing: %lu Hz / 3.000 s / gain=%.3f / %s\n",
                static_cast<unsigned long>(TRACK_HZ[index]),
                static_cast<double>(OUTPUT_GAIN), TRACKS[index]);
  return true;
}

void setup() {
  Serial.begin(115200); // GPIO1 / TX: debug only; GPIO3 / RX carries I2S data!
  delay(250);
  Serial.println();
  Serial.println(F("=== PCM5102A + TDA1308 5-frequency WAV test ==="));

  // Keep radio off to reduce supply noise and task interruptions.
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  delay(10);

  if (!LittleFS.begin()) {
    Serial.println(F("ERROR: LittleFS mount failed. Check FS partition/upload."));
    return;
  }

  // Validate everything before playback: no repeated failure loop.
  for (size_t i = 0; i < TRACK_COUNT; ++i) {
    if (!LittleFS.exists(TRACKS[i])) {
      Serial.printf("ERROR: Missing WAV in LittleFS: %s\n", TRACKS[i]);
      return;
    }
    File f = LittleFS.open(TRACKS[i], "r");
    if (!f || f.size() != WAV_BYTES) {
      Serial.printf("ERROR: Incorrect WAV size: %s (expected %lu bytes)\n",
                    TRACKS[i], static_cast<unsigned long>(WAV_BYTES));
      if (f) f.close();
      return;
    }
    f.close();
  }

  // The same I2S output class as in the source firmware, no PWM/no I2SNoDAC.
  audioLogger = &Serial;
  dac.SetGain(OUTPUT_GAIN);
  trackIndex = 0;
  ready = playTrack(trackIndex);
}

void loop() {
  if (!ready) {
    delay(1000);
    return;
  }

  if (player != nullptr && player->isRunning()) {
    if (player->loop()) {
      yield();
      return;
    }
  }

  // WAV EOF: AudioGeneratorWAV stopped the I2S driver.  Restart for next WAV.
  // A short clock gap between tracks is normal with this library.
  closeTrack();
  trackIndex = (trackIndex + 1) % TRACK_COUNT;
  ready = playTrack(trackIndex);
  yield();
}
