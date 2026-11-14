# MonoPod v0.8.9.1 — Arduino-ESP32 compilation hotfix

## Corrections
1. `DIY_iPod_Mono/DIY_iPod_Mono.ino`: explicitly include `WiFiNetworks.h` before using `wifiNetworks.begin()` in `setup()`.
2. `WiFiNetworks.h`: declare inline `hasSavedNetwork() const` for use from `$WIFI_STATUS`, using the already cached `savedSSID` (no additional NVS reads).
3. Mirror `WiFiNetworks.h` change in `Wokwi_ILI9341`.
4. Update boot version string to v0.8.9.1.
5. Add regression test that checks includes and public API in both firmware configurations, with a native C++ header compile probe.

## Unchanged
- GPIO / ESP32-S3 profile, NVS namespace and existing credentials, Wi-Fi boot reconnection, clicker fixes, microSD and battery diagnostics.
- No formatting or Flash erase required.

## Verification
- Python and native C++ regression tests run in the local build container.
- Full Arduino-ESP32 3.3.12 build and physical hardware check **not available in this environment**. Please compile and report any remaining messages.
