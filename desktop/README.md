# MonoPod Desktop

**Версия 1.3.0** · Windows 10/11 · C# / .NET 8 / Windows Forms

Приложение для управления музыкой и файлами на MonoPod с компьютера. Подключение поддерживается через USB CDC (протокол MPOD1) или Wi-Fi.

## Что умеет

- Просматривать папки и передавать файлы на microSD плеера.
- Работать с медиатекой MonoPodDB и тегами MP3/FLAC.
- Настраивать темы оформления плеера.
- Синхронизировать часы, управлять доступными настройками и запускать OTA с подтверждением на устройстве.

## Сборка

Потребуется **.NET SDK 8** или совместимый новый SDK. Откройте `MonoPod.Desktop.sln` в Visual Studio 2022 либо выполните команды **из директории `desktop/`**:

```powershell
dotnet restore .\MonoPod.Desktop.sln
dotnet build .\MonoPod.Desktop.sln -c Release
dotnet test .\MonoPod.Desktop.sln -c Release
dotnet publish .\MonoPod.Desktop\MonoPod.Desktop.csproj -c Release -r win-x64 --self-contained true -o .\publish\win-x64
```

Запускайте `publish\win-x64\MonoPod.Desktop.exe`. **Не переносите только .exe**: у приложения есть DLL и другие файлы.

## Подключение

**USB:** используйте USB-порт ESP32-S3 с Native USB CDC, а не диагностический USB-UART. В Desktop выберите порт и подключитесь; синхронизация запускается отдельной командой.

**Wi-Fi:** откройте Wi-Fi Sync на плеере, затем подключитесь по показанному им IP-адресу. Подтвердите сессию на устройстве, когда появится запрос.

## Где что лежит

- `MonoPod.Desktop/` — интерфейс приложения.
- `MonoPod.Core/` — транспорт, протокол, метаданные и бизнес-логика.
- `MonoPod.Desktop.Tests/` — автоматические тесты.
- `tools/` — инструменты разработки и сборки.
- `docs/` — [руководство пользователя](docs/USER_GUIDE_RU.md), [инструкция по сборке](docs/BUILD.md), [протокол](docs/PROTOCOL.md), [история релиза 1.3](docs/RELEASE_1.3_RU.md).
- `Legacy/Studio/` — старая Python-реализация для сравнения форматов (не нужна при запуске Desktop).

Готовые ZIP/EXE сборки не хранятся в Git: их удобнее публиковать во вкладке **Releases**. Аппаратное тестирование функций OTA и USB требуется проводить отдельно.
