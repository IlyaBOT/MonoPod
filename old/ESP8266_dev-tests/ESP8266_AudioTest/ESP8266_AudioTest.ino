/*
  MonoPod / ESP8266 + PCM5102A audio path test

  Startup -> one 1000 Hz beep over I2S DAC -> play /scars.mp3 once -> silence.

  Board: ESP8266 (NodeMCU/D1 mini with >=4 MB Flash)
  Library: ESP8266Audio by Earle F. Philhower III
  MP3: data/scars.mp3, upload with LittleFS filesystem uploader

  ESP8266 fixed I2S TX pins:
    D8 / GPIO15 -> PCM5102A BCK / BCLK
    D4 / GPIO2  -> PCM5102A LCK / LRCK / WS
    RX / GPIO3  -> PCM5102A DIN
    GND         -> PCM5102A GND
    3V3         -> PCM5102A VCC (for a 3.3V-capable breakout)

  GPIO3 is UART RX: some boards connect it to a USB-UART TX output.
  Beware of electrical contention when using I2S and serial interfaces.
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <math.h>

#include "AudioFileSourceLittleFS.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"

static const char *const AUDIO_FILE = "/New Fly Away.mp3";
static constexpr uint32_t BEEP_HZ = 800;
static constexpr uint32_t SAMPLE_RATE = 44100;
static constexpr uint32_t BEEP_MS = 150;
static constexpr float MUSIC_GAIN = 1.0f;  // start low; increase cautiously

AudioFileSourceLittleFS *trackFile = nullptr;
AudioGeneratorMP3 *decoder = nullptr;
AudioOutputI2S *dac = nullptr;
bool completed = false;

// Generate a 1 kHz, short, smooth-edged sine wave over the same I2S audio
// path which will later carry the MP3.  No piezo / GPIO PWM is required.
bool playStartupBeep(AudioOutputI2S &audio) {
  audio.SetRate(SAMPLE_RATE);
  audio.SetChannels(2);
  audio.SetGain(MUSIC_GAIN);
  if (!audio.begin()) {
    Serial.println(F("ERROR: I2S init failed"));
    return false;
  }

  const uint32_t samples = (SAMPLE_RATE * BEEP_MS) / 1000;
  const float phaseStep = 2.0f * PI * (float)BEEP_HZ / (float)SAMPLE_RATE;
  for (uint32_t i = 0; i < samples; ++i) {
    // 5 ms linear attack/release prevents clicks at the start/end.
    const uint32_t fadeSamples = SAMPLE_RATE / 200;
    float env = 1.0f;
    if (i < fadeSamples) env = (float)i / (float)fadeSamples;
    if (samples - i < fadeSamples) {
      const float release = (float)(samples - i) / (float)fadeSamples;
      if (release < env) env = release;
    }
    const int16_t s = (int16_t)(sinf(phaseStep * i) * 8500.0f * env);
    int16_t stereo[2] = {s, s};
    while (!audio.ConsumeSample(stereo)) {
      yield();
    }
    if ((i & 1023U) == 0) yield();
  }

  // Short tail of digital zero samples to drain the audio FIFO.
  int16_t silence[2] = {0, 0};
  for (uint32_t i = 0; i < SAMPLE_RATE / 50; ++i) {
    while (!audio.ConsumeSample(silence)) yield();
  }
  delay(25);
  audio.stop();
  return true;
}

void setup() {
  Serial.begin(115200);  // UART TX (GPIO1) is still usable for status messages
  delay(300);
  Serial.println();
  Serial.println(F("=== MonoPod ESP8266 I2S audio test ==="));

  // Avoid Wi-Fi/background work competing with MP3 decoding on ESP8266.
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  delay(10);

  if (!LittleFS.begin()) {
    Serial.println(F("ERROR: LittleFS mount failed. Check Flash Size / FS partition."));
    completed = true;
    return;
  }
  if (!LittleFS.exists(AUDIO_FILE)) {
    Serial.println(F("ERROR: /scars.mp3 missing. Upload the 'data' folder with LittleFS uploader."));
    completed = true;
    return;
  }

  Serial.print(F("File size: "));
  File testFile = LittleFS.open(AUDIO_FILE, "r");
  if (!testFile) {
    Serial.println(F("ERROR: cannot open MP3"));
    completed = true;
    return;
  }
  Serial.print(testFile.size());
  Serial.println(F(" bytes"));
  testFile.close();

  audioLogger = &Serial;
  dac = new AudioOutputI2S();
  if (!dac) {
    Serial.println(F("ERROR: allocation failed (I2S)"));
    completed = true;
    return;
  }

  Serial.println(F("Beep: 1000 Hz, 150 ms, via PCM5102A"));
  if (!playStartupBeep(*dac)) {
    completed = true;
    return;
  }

  trackFile = new AudioFileSourceLittleFS(AUDIO_FILE);
  decoder = new AudioGeneratorMP3();
  if (!trackFile || !decoder || !trackFile->isOpen()) {
    Serial.println(F("ERROR: MP3 source/decoder init failed"));
    completed = true;
    return;
  }

  dac->SetGain(MUSIC_GAIN);
  Serial.println(F("Playing scars.mp3 once..."));
  if (!decoder->begin(trackFile, dac)) {
    Serial.println(F("ERROR: decoder->begin() failed"));
    completed = true;
  }
}

void loop() {
  if (completed) {
    delay(1000);
    return;
  }

  // No auto-repeat: after reaching EOF we never start the decoder again.
  if (decoder != nullptr && decoder->isRunning()) {
    if (!decoder->loop()) {
      decoder->stop();
      completed = true;
      Serial.println(F("Playback finished. Silence until reset."));
    }
  } else {
    completed = true;
    Serial.println(F("Playback stopped."));
  }
  yield();
}
