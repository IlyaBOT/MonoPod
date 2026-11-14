# MonoPod v0.8.3 — подключение ESP32-S3 N16R8 (макет)

**Статус:** проектная распиновка; аппаратная проверка не выполнена. Точная ревизия DevKit, схемы USB-C и RGB может отличаться. Сверяй маркировки GPIO с платой.

## Текущая сборка Rev. A-P

ESP32-S3 N16R8, 16 МБ Flash, ~8 МБ PSRAM, ST7735S 1.8", microSD, PCM5102A, TDA1308 после проверки, 6-секторное ёмкостное колесо, 5 кнопок через ADC1, HOLD, **пассивная пьезоэлектрическая пищалка для звукового щелчка**. **Вибромотора, haptic-привода, DRV2605L и дополнительного механического энкодера в макете нет.**

| Соединение | GPIO | Замечание |
|---|---:|---|
| TFT+microSD SCK | 12 | общая SPI-шина |
| TFT+microSD MOSI | 11 | общая SPI-шина |
| microSD MISO | 13 | TFT MISO обычно не требуется |
| TFT CS | 10 | активный LOW |
| TFT D/C | 39 | Data/Command |
| TFT RST | 47 | обязательно физически подключить |
| microSD CS | 14 | отдельный сигнал |
| PCM5102A BCK | 16 | I²S BCLK |
| PCM5102A LRCK | 17 | I²S WS/LRCK |
| PCM5102A DIN | 18 | I²S DATA |
| Пять кнопок, лестница сопротивлений | 9 | ADC1, питание от 3.3 В |
| Аккумулятор (через делитель 100k/100k) | 4 | ADC1, 100 нФ к GND |
| HOLD | 21 | внешняя подтяжка 10k к 3.3 В |
| Сенсоры Click Wheel (6) | 1, 2, 5, 6, 7, 8 | **S3 touchRead при касании увеличивается** |
| Звуковой пьезоэлемент | 42 | GPIO → высокоомная пьеза → GND (проверить модель/ток) |
| USB native D− / D+ | 19 / 20 | уже разведены к **нативному** USB-C на DevKit; не подключать вручную к разъёму напрямую |

**GPIO41 не используется для iAP1 или USB.** GPIO40/41 не подключать: это резерв на будущую ревизию, но сейчас DRV2605L отсутствует. GPIO35–37 могут обслуживать OPI PSRAM, GPIO38/48 — встроенный RGB на отдельных ревизиях. Эти выводы не занимать. Второй USB-C обычно подключён к USB-UART-мосту, а не к нативному USB; конкретный порт определять по схеме/диспетчеру устройств.

## Arduino IDE

1. `ESP32 by Espressif Systems` 3.x; Board: `ESP32S3 Dev Module`.
2. Flash Size: `16MB`; PSRAM: `OPI PSRAM` **если фактически N16R8**.
3. USB Mode: `Hardware CDC and JTAG`; USB CDC On Boot: `Enabled`; Upload Mode по конкретной плате (`UART0 / Hardware CDC`).
4. В `Config.h` автопрофиль S3 или явное `MONOPOD_BOARD_ESP32_S3`; `FEATURE_USB_SYNC=1` (по умолчанию), `FEATURE_BLUETOOTH=0` принудительно, `CLICKER_HARDWARE=CLICKER_HW_PIEZO`.
5. Дисплей/SD/ЦАП подключать после первой компиляции; первое включение **без Li-ion аккумулятора** от USB DevKit.

## USB-синхронизация

- Нативный USB-C ESP32-S3 (D− GPIO19, D+ GPIO20) выдаёт **USB CDC COM-порт**. Это не накопитель USB Mass Storage и не Apple iAP/iTunes.
- В прошивке `UsbSync.cpp` реализован собственный `MonoPod USB Sync v1`: `HELLO`, `INFO`, `PUT`, `SCAN`. Треки записываются в `/Music` на microSD, CRC32 проверяется до переименования временного файла.
- Диагностический Serial перенаправлен на `Serial0` (USB-UART-порт), чтобы сообщения не повреждали протокол USB.
- На ПК установить `pip install -r Studio/requirements.txt`; в **MonoPod Studio → Device & Music → USB CDC port** выбрать `COMx`/`/dev/ttyACM*`, нажать USB Connect, затем Upload over USB-C.
- CLI: `python Studio/usb_sync.py ports` → `python Studio/usb_sync.py --port COM7 info` → `python Studio/usb_sync.py --port COM7 send song1.mp3 song2.flac`.
- Во время передачи отключить Wi-Fi Sync; не извлекать microSD, не открывать COM-порт одновременно в Serial Monitor и Studio. На S3 с включённым USB Sync **Serial Monitor нужно подключать через USB-UART**, не через нативный USB CDC.

## Питание

Зарядка аккумулятора 1S — **отдельный модуль TP4056**, не от USB-портов DevKit. Нагрузка через защиту и выключатель к **отдельному стабилизированному DC/DC 3.3 В**. TP4056 не имеет power-path: зарядка с выключенным плеером. Перед подключением аккумуляторного стабилизатора к DevKit исключить одновременную подачу питания с USB или выполнить обоснованную развязку (защита от обратных токов). Для синхронизации на макете использовать USB DevKit при отключённом аккумуляторном DC/DC.

## Будущая Rev. B PCB

Возможны BQ24074 для зарядки с power-path и DRV2605L + LRA **вместо звуковой пьезы**, но **не входят в BOM или разводку текущего прототипа**. USB Mass Storage / iTunes — отдельная исследовательская задача; текущий USB Sync не притворяется ими.
