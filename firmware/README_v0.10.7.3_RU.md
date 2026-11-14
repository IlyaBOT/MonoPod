# MonoPod v0.10.7.3 — Wi-Fi Consent, Clock, Experimental USB MSC

Исходник: v0.10.7.2. Сборка ESP32-S3 N16R8. Не включает бинарники Apple.

### Изменения

- При соединении с Wi-Fi плеер обнаруживается по mDNS `_monopod._tcp.local` и принимает запрос `POST /api/sync/request` без немедленного входа в режим SYNC.
- На TFT диалог разрешения/отказа: Wheel или PREV/NEXT, SELECT, Play=yes, MENU=no. 30 секунд ожидания, по умолчанию NO. Одобренный клиент получает одноразовый session token; его и IP необходимо подтверждать во всех API действиях. Legacy ручной Wi-Fi Sync/AP оставлен.
- Часы на главном экране вместо iPod после настройки времени; выбор 12/24 ч в Settings → Time Format; сохраняется в NVS.
- Settings → USB Mode: Mass Storage / iTunes (Beta); выбор сохраняется. Добавлен модуль UsbMassStorage, но по соображениям сохранности SD отключён по умолчанию: см. docs/WIFI_CLOCK_USB_V01073_RU.md. Стандартная сборка всё ещё использует старый USB CDC MPOD1.
- Подключение iPod-подобной структуры SysInfo при экспериментальной iTunes mode. Каталог iPod_Control/Music теперь участвует в индексации вместе с Music.

### Статус

- Python-тесты: 61 passed, 1 skipped. ClockService: 26 native cases; ClockConfigValidation: 32; WheelDetents: 4.
- Arduino-ESP32 сборка, USB MSC на реальном устройстве, iTunes 7/8/9 **не проверены**.
- Нет парсера/записи iTunesDB на устройстве. Репликация оригинального iPod в iTunes пока **экспериментальная, неполная**; подмена VID/PID сама по себе недостаточна.
- Не использовать основной носитель/единственную копию музыки при проверке MSC. Для OTA и SD писать только при стабильном питании 3,3 В.

Дополнительные материалы: `docs/WIFI_CLOCK_USB_V01073_RU.md`, `docs/CODEX_DESKTOP_WIFI_AND_ITUNES_RU.md`, `tools/wifi_consent_client.py`.
