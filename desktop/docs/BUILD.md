# Сборка и тестирование MonoPod Desktop

Текущая версия: **1.3.0**. Платформа: Windows 10/11 x64, .NET 8, Windows Forms.

## Что установить

- .NET SDK 8 (или совместимый новый SDK);
- Visual Studio 2022 с компонентом .NET desktop development — если нужна IDE.

Из папки `desktop/`:

```powershell
dotnet restore .\MonoPod.Desktop.sln
dotnet build .\MonoPod.Desktop.sln -c Release
dotnet test .\MonoPod.Desktop.sln -c Release
dotnet publish .\MonoPod.Desktop\MonoPod.Desktop.csproj -c Release -r win-x64 --self-contained true -o .\publish\win-x64
```

Исполняемый файл: `publish\win-x64\MonoPod.Desktop.exe`. Публикация создаёт **папку** с несколькими файлами, а не одиночный EXE.

Некоторые тесты, обрабатывающие реальные аудиофайлы, могут потребовать FFmpeg. Большинство тестов используют имитаторы устройств, поэтому прохождение тестов не подтверждает USB/OTA на реальном железе.

Чтобы упаковать уже опубликованное приложение в ZIP, используйте `python tools/package_desktop.py`. Архив появится в `desktop/outputs/`, которая исключена из Git.

## Прошивка из MonoPod Desktop

Исходники прошивки находятся **не внутри Desktop**, а в соседней папке `../firmware/DIY_iPod_Mono/`. Для обычной сборки лучше открыть её в Arduino IDE. Вспомогательный скрипт `tools/build_firmware.ps1` также использует этот путь.

Описание синхронизации: [PROTOCOL.md](PROTOCOL.md). Использование приложения: [USER_GUIDE_RU.md](USER_GUIDE_RU.md).

Исторические отчёты и старые версии не следует воспринимать как инструкцию для текущей прошивки.
