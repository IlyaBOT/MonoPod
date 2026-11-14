# MonoPod v0.9.1.1 — Wi-Fi Sync LAN regression fix

- Fix: Starting Wi-Fi Sync on an already-connected ESP32 no longer calls `WiFi.mode(WIFI_AP)` or disassociates it from its router.
- Sync HTTP API now listens on port 80 of the active STA interface. ESP32 retains the same LAN address. Router reconnect attempts continue during Sync.
- AP fallback (`MonoPod-Sync`, `192.168.4.1`) remains available when the STA has no connection; starting Sync during an in-progress reconnect no longer prematurely flips to AP mode.
- Stopping a LAN Sync never changes the Wi-Fi radio mode or disconnects the station. Stopping an AP Sync restores saved STA credentials.
- Display shows the actual Sync server IP; UART log prints LAN/AP mode and full URL.
- Studio now explains connection refused and asks for the address displayed on the player.
- Explicit Sync Mode, no concurrent SD access by playback, safe upload and exit rules retained.

Verification: pytest regression suite; Arduino-ESP32 3.3.12 hardware compile and live transfer not yet executed.
