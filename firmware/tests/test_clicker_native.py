from pathlib import Path
import shutil
import subprocess

import pytest


FIRMWARE = Path(__file__).resolve().parents[1] / "DIY_iPod_Mono"


def test_gpio_click_queue_and_idle_audio(tmp_path):
    compiler = shutil.which("g++")
    if compiler is None:
        pytest.skip("g++ is needed for the native clicker test")
    # Compile the real controllers with small Arduino, ESP timer and audio shims.
    shims = {
        "Arduino.h": """
            #pragma once
            #include <cstddef>
            #include <cstdint>
            #include <string>
            using String = std::string;
            enum { LOW, HIGH, OUTPUT };
            uint32_t millis();
            void pinMode(int, int);
            void digitalWrite(int, int);
            struct Log { void println(const char*) {} };
            inline Log Serial0;
        """,
        "esp_timer.h": """
            #pragma once
            #include <cstdint>
            enum { ESP_OK, ESP_TIMER_TASK };
            struct esp_timer_create_args_t {
                void (*callback)(void*);
                void* arg;
                int dispatch_method;
                const char* name;
                bool skip_unhandled_events;
            };
            using esp_timer_handle_t = esp_timer_create_args_t*;
            int esp_timer_create(const esp_timer_create_args_t*, esp_timer_handle_t*);
            int esp_timer_start_once(esp_timer_handle_t, uint64_t);
        """,
        "Settings.h": """
            #include "Types.h"
            inline struct Settings {
                struct { ClickerMode clicker = ClickerMode::Buzzer; } data;
                void markDirty() {}
            } settings;
        """,
        "SyncMode.h": """
            inline struct Sync {
                bool running = false;
                bool active() const { return running; }
            } syncMode;
        """,
        "AudioEngine.h": """
            inline struct Player {
                int clicks = 0;
                void headphoneClick(int, int) { ++clicks; }
                void stopHeadphoneClick() {}
            } player;
        """,
        "AudioOutput.h": """
            #pragma once
            #include <cstdint>
            class AudioOutput {
            public:
                virtual bool SetRate(int) { return true; }
                virtual bool SetChannels(int) { return true; }
                virtual bool SetGain(float) { return true; }
                virtual bool begin() = 0;
                virtual bool stop() = 0;
                virtual bool ConsumeSample(int16_t[2]) = 0;
            };
        """,
    }
    for name, source in shims.items():
        (tmp_path / name).write_text(source, encoding="utf-8")
    shutil.copyfile(FIRMWARE / "Feedback.cpp", tmp_path / "Feedback.cpp")
    executable = tmp_path / "clicker-test.exe"
    subprocess.run([
        compiler, "-std=c++20", "-DCONFIG_IDF_TARGET_ESP32S3", "-DARDUINO_USB_CDC_ON_BOOT=1",
        "-I", str(tmp_path), "-I", str(FIRMWARE),
        str(tmp_path / "Feedback.cpp"), str(FIRMWARE / "ClickMixOutput.cpp"),
        str(Path(__file__).with_name("clicker_native.cpp")),
        "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
