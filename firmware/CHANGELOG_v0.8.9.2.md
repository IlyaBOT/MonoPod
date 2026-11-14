# MonoPod v0.8.9.2 — production ADC/SD cleanup

- Resolved ADC misread was caused by missing common battery ground; **normal battery monitoring is retained**.
- Resolved SD mount failure was caused by incorrect SD CS wiring; single fixed-frequency SD.begin now replaces the three-rate mount attempts.
- Configurable SPI speed: `Config::SPI_SD_HZ` defaults to 4,000,000 Hz; the 400 kHz independent diagnostic success does not by itself verify 4 MHz in the finished wiring.
- Removed diagnostic console commands `$ADC_DIAG` and `$SD_RETRY` and their parser branches.
- Kept simple read-only `$BATTERY` (voltage / percent) and `$SD` (mount status / capacity) commands and `FEATURE_SD_FORMAT` safety guard.
- Removed the obsolete standalone ADC/SD one-off diagnostic sketch from the production distribution. The separate DevTest v1 project remains available independently.
- Synced Wokwi source tree and updated automated regression tests.

Full Arduino-ESP32 compilation and on-device validation are still pending.
