# MonoPod v0.10.6 — INPUT_PULLUP / Background Playback / MPOD1 CLI

База: MonoPod Firmware v0.10.5 (BAV3 RGB565/GRAY6 SPI). Протокол: MPOD1 v4 без смены wire-format.

## Прошивка

- ESP32-S3 GPIO9: `INPUT_PULLUP`, внутренний pull-up GPIO дополнительно активируется после конфигурации ADC, внешний 10 кΩ на GPIO9 более не требуется.
- Сохранены прежние ветки кнопок к GND: MENU=0Ω, PREV=2.2кΩ, NEXT=5.1кΩ, PLAY/PAUSE=10кΩ, SELECT=22кΩ.
- Ожидаемые напряжения (только при номинальном Rpull≈45кΩ и 3.3В): 0 / 0.154 / 0.336 / 0.600 / 1.084 В. В реальности будут отличаться: внутренний pull-up имеет большой разброс, ADC на нижнем диапазоне с 11 dB attenuation неточен. Пороговые значения в `Config.h` `BUTTON_ADC_BOUNDS_MV` = {75,245,470,795,1550} мВ — стартовые, НЕ откалиброванные.
- Для обычного ESP32 GPIO34 не имеет внутренней подтяжки; оставлено исходное `INPUT` и внешний 10 кΩ.
- Диагностика: через *UART0* (`Serial0`, 115200), отправить `$BUTTON_ADC` и измерить отдельно в покое, MENU, PREV, NEXT, PLAY, SELECT. При ошибках распознавания скорректировать `Config.h`. Невозможно гарантировать стабильность аналоговой лестницы с некалиброванным внутренним pull-up.
- Воспроизведение музыки было фоновым уже в v0.10.5: `player.loop()` запускается независимо от страницы. Теперь иконка PLAY/PAUSE видна в других меню, а `Now Playing` всегда последний пункт главного меню после `Backlight`. При отсутствии трека показывается `No music playing`.
- На статус-баре HOLD имеет приоритет. Wi-Fi-пиктограмма показывается, когда плеер ничего не воспроизводит. Во время воспроизведения/паузы отображается соответствующая пиктограмма.
- Убран запрет общего файлового API для строго перечисленного пользовательского файла `/MonoPod/badapple.bav` и разрешён просмотр/создание каталога `/MonoPod`. Другие файлы внутри `/MonoPod` остаются защищёнными; загрузка темы по-прежнему через специальный API.

## MPOD1 CLI

Требуется Python 3.10+; Wi-Fi использует только Python stdlib. Для USB установить `python3 -m pip install pyserial`.

Пример **с телефона по Wi-Fi** (Termux / любая среда Python 3 с сетевым доступом):

```bash
python3 cli/monopod.py --wifi http://192.168.1.123 info
python3 cli/monopod.py --wifi http://192.168.1.123 ls /
python3 cli/monopod.py --wifi http://192.168.1.123 ls /MonoPod
python3 cli/monopod.py --wifi http://192.168.1.123 put badapple.bav /MonoPod/badapple.bav --replace
python3 cli/monopod.py --wifi http://192.168.1.123 upload song1.mp3 song2.mp3
python3 cli/monopod.py --wifi http://192.168.1.123 get /MonoPod/badapple.bav ./badapple-backup.bav
```

На плеере предварительно выбрать `Settings → Wi-Fi → Start Wi-Fi Sync`. В режиме Wi-Fi Sync аудио останавливается и обычное управление блокируется; это намеренное исключение из фонового воспроизведения.

Пример через Native USB ESP32-S3:

```bash
python3 -m pip install pyserial
python3 cli/monopod.py --usb /dev/cu.usbmodem101 ls /Music
python3 cli/monopod.py --usb /dev/cu.usbmodem101 put badapple.bav /MonoPod/badapple.bav --replace
```

Для USB используется native USB CDC Full Speed, а не аппаратный UART. `--baud 115200` — параметр CDC line coding, скорость физической USB-линии не ограничивает. Диагностический UART0 и USB Sync — разные порты.

Операции CLI: `info`, `ls`, `stat`, `put`, `upload`, `get`, `mkdir`, `rm`, `mv`, `scan`, `sync-end`. Объём/CRC проверяет прошивка после загрузки, скачивание проверяется CRC каждого блока (1024 байта USB; 16 384 байта Wi-Fi). Загрузка музыки в `/Music` вызывает повторное сканирование; загрузка BAV3 — нет.

Ограничения: прошивка MPOD1 v0.10.5 ещё **не разрешает** запись в `/MonoPod/badapple.bav`. Для этой команды нужно прошить v0.10.6. Запись произвольных файлов разрешена вне защищённых областей, но `/MonoPod` специально ограничен. Не используйте одновременно USB и Wi-Fi Sync.

## Проверки

- Python тесты прошивки: 25 passed / 1 skipped.
- Python тесты терминального клиента на эмуляторах USB CDC и HTTP: 12 passed.
- Native C++ тесты: BavPixel, ID3, MarqueeMotion пройдены.
- Не запускались: полный Arduino-ESP32 build, аппаратный ADC тест и реальная передача на ESP32-S3.
- Архив MonoPod Desktop (Windows Forms) не был доступен среди вложений при подготовке этого обновления: реализация CLI проверена на исходном MPOD1 v4 (`UsbSync.cpp`, `WiFiSync.cpp`, `SyncFiles.cpp`), а не сопоставлена с неопубликованным клиентом.
