# MonoPod v0.8.7 — runtime UART development console

- Added `$DISABLE_HARDWARE_CONTROL=1` / `=0` to disable and re-enable hardware controls **at runtime** (does not persist after reboot).
- Added console navigation commands for Menu, Select, long Select, Up/Down, Prev/Next, Play/Pause and scroll batches.
- On ESP32-S3, uses `Serial0` (UART0 / UART-to-USB bridge) for console and preserves native USB `Serial` exclusively for MPOD1 USB sync.
- Bypasses floating/unassembled HOLD and ADC/touch input when controls are disabled, resets input debounce state on mode changes.
- Adds `$BATTERY` raw ADC measurement, `$SD` diagnostics, `$STATUS` and `$HELP`.
- Sync Mode rejects console navigation except the existing allowed Wi-Fi long Select when transfer is idle; USB sessions still require host-side `SYNC END`.
- Preserved PWM backlight and v0.8.6 changes.
- Native protocol parser and Python structural regression tests added; full Arduino build/hardware verification still pending.
