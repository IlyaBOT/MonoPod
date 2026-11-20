#include "Feedback.h"
#include "ClickMixOutput.h"
#include "AudioEngine.h"
#include "SyncMode.h"
#include <cassert>
#include <utility>
#include <vector>

static uint32_t now = 0;
static uint32_t due = 0;
static bool armed = false;
static int level = LOW;
static esp_timer_create_args_t clockTimer{};
static std::vector<std::pair<uint32_t, int>> edges;

uint32_t millis() { return now; }
void pinMode(int pin, int mode) { assert(pin == 42 && mode == OUTPUT); }
void digitalWrite(int pin, int next) {
  assert(pin == 42);
  if (next != level) edges.emplace_back(now, next);
  level = next;
}
int esp_timer_create(const esp_timer_create_args_t* args, esp_timer_handle_t* timer) {
  clockTimer = *args;
  *timer = &clockTimer;
  return ESP_OK;
}
int esp_timer_start_once(esp_timer_handle_t timer, uint64_t us) {
  assert(timer == &clockTimer && !armed);
  due = now + us / 1000;
  armed = true;
  return ESP_OK;
}
static void advance(uint32_t end) {
  if (armed && due <= end) {
    now = due;
    armed = false;
    clockTimer.callback(clockTimer.arg);
  }
  now = end;
}

static void testBuzzer() {
  feedback.begin();
  for (auto mode : {ClickerMode::Off, ClickerMode::Buzzer,
                    ClickerMode::Headphones, ClickerMode::Both}) {
    assert(feedback.available(mode));
  }
  feedback.cursorMoved(0);
  assert(edges.empty());
  feedback.cursorMoved(3);
  advance(59);
  assert(level == HIGH);
  advance(60); // Timer ends the pulse even without calling feedback.loop().
  assert(level == LOW);
  advance(71);
  feedback.loop();
  assert(level == LOW);
  advance(72);
  feedback.loop();
  advance(144);
  feedback.loop();
  advance(300);
  feedback.loop();
  const std::vector<std::pair<uint32_t, int>> expected = {
    {0, HIGH}, {60, LOW}, {72, HIGH}, {132, LOW}, {144, HIGH}, {204, LOW},
  };
  assert(edges == expected && player.clicks == 0);

  feedback.setMode(ClickerMode::Headphones);
  feedback.cursorMoved();
  assert(player.clicks == 1 && edges == expected);
  feedback.setMode(ClickerMode::Both);
  feedback.cursorMoved(3);
  assert(player.clicks == 2 && level == HIGH);
  feedback.setRuntimeMuted(true);
  assert(level == LOW);
  feedback.cursorMoved();
  feedback.setRuntimeMuted(false);
  advance(500);
  feedback.loop();
  assert(level == LOW && edges.size() == expected.size() + 2 && player.clicks == 2);

  syncMode.running = true;
  feedback.cursorMoved();
  assert(level == LOW && player.clicks == 2);
  syncMode.running = false;
  feedback.setMode(ClickerMode::Off);
  feedback.cursorMoved();
  assert(level == LOW && player.clicks == 2);
}

struct AudioSink : AudioOutput {
  bool blocked = false;
  int stops = 0;
  std::vector<int16_t> samples;
  bool begin() override { return true; }
  bool stop() override { ++stops; return true; }
  bool ConsumeSample(int16_t sample[2]) override {
    if (blocked) return false;
    samples.push_back(sample[0]);
    return true;
  }
};

static void testIdleAudio() {
  AudioSink target;
  ClickMixOutput output(&target, 4);
  output.SetRate(1000);
  output.trigger(300, 2);
  output.serviceIdle(2);
  assert(target.samples.size() == 2 && target.samples.front() != 0);
  target.blocked = true;
  output.serviceIdle();
  assert(target.stops == 0);
  target.blocked = false;
  output.serviceIdle(3);
  assert(target.stops == 0);
  output.serviceIdle();
  assert(target.stops == 1 && target.samples.size() == 6);

  output.trigger(300, 2);
  output.serviceIdle(1);
  int16_t music[2] = {123, -456};
  assert(output.ConsumeSample(music));
  assert(target.stops == 2 && target.samples.back() == 123);
  assert(output.ConsumeSample(music));
  assert(target.stops == 2);
}

int main() {
  testBuzzer();
  testIdleAudio();
}
