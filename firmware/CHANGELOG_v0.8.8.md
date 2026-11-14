# MonoPod v0.8.8 — Hardware diagnostic fixes

- Runtime hardware-disable now also silences piezo and I2S clicker; added `$CLICKER_MUTE=1|0`, `$CLICKER_STATUS` and click counter.
- Shared-SPI microSD initialized before TFT, with non-destructive 4MHz/1MHz/400kHz mount attempts and UART `$SD_RETRY` / detailed `$SD`.
- Formatting still DISABLED and not attempted. The format-menu label now clearly states unavailable.
- ADC battery measurement reapplies 11 dB channel attenuation, discards first reading; `$BATTERY` prints both raw ADC and measured millivolts. **100nF capacitor across GPIO4-GND is recommended for 100k/100k divider.**
- Check hardware manually before declaring SD or ADC fixed.
