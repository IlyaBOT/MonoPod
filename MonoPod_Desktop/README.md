# MonoPod Desktop 1.3.0

MonoPodDB v1, virtual local library grid and real MP3/FLAC metadata editing. See [release notes](docs/RELEASE_1.3_RU.md). Ready-to-run x64 build: `publish/win-x64-1.3.0/MonoPod.Desktop.exe`. Earlier release folders are preserved.

Native C# / .NET 8 Windows Forms client for MonoPod. No Python, browser engine or web UI is used at runtime.

Open `MonoPod.Desktop.sln` in Visual Studio 2022 17.8 or later (17.14 tested). Start project: `MonoPod.Desktop`. Windows 10/11 x64.

```powershell
dotnet restore
dotnet build -c Release
dotnet test -c Release
dotnet publish -c Release -r win-x64 --self-contained true
```

Portable application: `publish\win-x64\MonoPod.Desktop.exe`. Keep all files in that directory together. The self-contained output includes the .NET runtime. Build tools can use .NET SDK 8 or a newer compatible SDK; the app targets .NET 8.

Desktop 1.2.1 displays the device's local clock and civil UTC offset (Moscow: UTC+03:00), decoding the POSIX timezone returned by the player. Version 1.2.0 introduced manual local-file Wi-Fi OTA for firmware 0.10.6.6 / OTA v1, PC clock synchronization over USB or Wi-Fi, device NTP settings and optional connection actions. Media-index and time automation are off by default; timezone follows Windows unless manually selected. Existing USB CDC MPOD1 v4, themes, paced writes and cancellation recovery remain covered by regressions. Legacy Studio and older firmware trees were preserved. No device was flashed or used for OTA/NTP testing in this release. The earlier 32 KiB physical transfer/readback belongs to Desktop 1.1.1.

Features: USB CDC MPOD1 v4 / local Wi-Fi HTTP; paginated microSD explorer; streaming verified transfers and atomic replacement; Unicode paths; music metadata in independent columns; MPT1/MPT2 theme editor and 160×128 preview; upload queue; cancellation; JSON preferences; exportable log. Older firmware is capability-limited and displays Unsupported for unavailable operations.

OTA: Device → Firmware Update while connected to Wi-Fi Sync; choose the application/sketch `.bin`, enter the screen PIN and explicitly confirm. Upload completion, image acceptance and running-version verification are separate stages. After reboot, reopen Wi-Fi Sync on MonoPod and reconnect. PINs are never saved in preferences or logs.

- [Local time and Moscow timezone fix: 1.2.1](docs/RELEASE_1.2.1_RU.md)
- [OTA, clock and NTP: 1.2.0](docs/RELEASE_1.2_RU.md)
- [Keep USB connected after cancellation: 1.1.2](docs/CANCEL_CONNECTION_FIX_RU.md)
- [USB BAV timeout hotfix 1.1.1](docs/BAV_USB_FIX_RU.md)
- [Desktop 1.1 changes and validation](docs/RELEASE_1.1_RU.md)
- [Historical Desktop 1.0 report](docs/FINAL_REPORT_RU.md)
- [User guide](docs/USER_GUIDE_RU.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Protocol and MPT byte layout](docs/PROTOCOL.md)
- [Build instructions](docs/BUILD.md)
- [Verification and hardware checklist](docs/VALIDATION_RU.md)

`Legacy/Studio` contains the latest original Python Studio; `Legacy/Studio_v0.9.0` preserves the initially found reference. They are development references and are not part of the published application.
