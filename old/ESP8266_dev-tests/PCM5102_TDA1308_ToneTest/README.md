# ESP8266 -> PCM5102A -> TDA1308 signal diagnostic

## Files
- PCM5102_TDA1308_ToneTest.ino — ESP8266 firmware (Arduino IDE)
- data/tone_500.wav — 500 Hz sine, 3 s
- data/tone_1000.wav — 1000 Hz sine, 3 s
- data/tone_5000.wav — 5000 Hz sine, 3 s
- data/tone_10000.wav — 10000 Hz sine, 3 s
- data/tone_20000.wav — 20000 Hz sine, 3 s

WAV format: mono / 16-bit signed little-endian PCM / 48,000 samples/s.
Sample amplitude: 80% of 16-bit full scale (constant throughout every WAV, no fades).
All five WAVs use the same level. Each is 288,044 bytes; total 1,440,220 bytes.

## Wiring (taken from prior firmware)
- D8 (GPIO15) -> PCM5102A BCK
- D4 (GPIO2)  -> PCM5102A LCK/LRCK
- RX (GPIO3) -> PCM5102A DIN
- GND -> GND
- 3V3 -> VCC ONLY if the DAC breakout supports 3.3 V.

## Setup
1. Use ESP8266 (NodeMCU/D1 mini 4 MB or larger) with ESP8266 Arduino core.
2. Install Earle F. Philhower III's `ESP8266Audio` library.
3. Choose a board flash layout with at least **2 MB LittleFS** (1.44 MB of WAV data).
4. Upload the sketch, then upload the **data/** folder to LittleFS with an ESP8266 LittleFS uploader.
   If the uploader replaces firmware or does not support your board, verify its flash layout and upload method first.
5. Reboot and open Serial Monitor at 115200 baud: it prints each test frequency.
6. The frequency sequence repeats forever: 500 -> 1000 -> 5000 -> 10000 -> 20000 -> 500 Hz ...

## Gain
Change `static constexpr float OUTPUT_GAIN = 0.20f;` in the sketch and reflash.
0.0 = muted; 1.0 = unattenuated WAV. For first measurements use 0.10-0.20.
DAC approximate expected amplitude at 2.1 Vrms full-scale:
 V_RMS ~ 2.1 * 0.80 * OUTPUT_GAIN (for low/mid audio frequencies into suitable load).
Thus at gain=0.20 the expected output is about 0.34 Vrms / 0.95 Vpp.
At high frequencies the analog reconstruction filter may change amplitude or waveform.

## Oscilloscope
- Use 10x probe, DC coupling for checking DC offset; AC coupling for ripple if desired.
- Check PCM5102A LOUT or ROUT versus GND first, then input and output on TDA1308 board.
- Some TDA1308 circuits use supply mid-point bias and AC coupling; check schematic.
- Directly connecting a full-scale PCM5102A output to a 3.3 V TDA1308 stage can cause clipping.
- DO NOT use 20 kHz at high gain into headphones/ears. Test without headphones initially.

## Note
The WAV library stops/restarts I2S at each 3 s file boundary. Small transitions/gaps
between files are normal and are not defects in the steady sinusoid.
ESP8266 GPIO3 is USB-UART RX and may be actively driven by USB serial chip TX;
check for bus contention if the DAC receives bad samples.
