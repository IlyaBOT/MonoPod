# Build and developer verification

Desktop 1.3.0 adds the separate TagLibSharp 2.3.0 NuGet library to Core. Build/test remains .NET only. Run `dotnet publish MonoPod.Desktop/MonoPod.Desktop.csproj -c Release -r win-x64 --self-contained true -o publish/win-x64-1.3.0` for the versioned distribution. The two real-audio fixture tests require FFmpeg (currently C:\ffmpeg\bin\ffmpeg.exe); other tests use HTTP/USB device doubles. See RELEASE_1.3_RU.md and validation-1.3.0. The material below describes earlier versions where explicitly versioned.

Windows 10/11 x64, Visual Studio 2022 17.8+ with .NET desktop workload, .NET SDK 8+ and Windows targeting packs. This machine was verified with SDK 9.0.300 and VS 2022 MSBuild 17.14; all projects target .NET 8 / C# 12.

From the solution directory:

```powershell
dotnet restore
dotnet build -c Release
dotnet test -c Release
dotnet publish -c Release -r win-x64 --self-contained true
```

Output: `publish\win-x64\MonoPod.Desktop.exe`. Publishing/trimming/single-file settings are explicit; this is an ordinary folder deployment. Only System.IO.Ports 8.0.0 is required by the app. Test packages are Microsoft.NET.Test.Sdk 17.11.1, xUnit 2.9.2 and VS adapter 2.8.2. Core and test projects are not separately publishable. No runtime Python dependency exists.

Current Desktop 1.2.0 validation targets firmware 0.10.6.6 / MPOD1 v4 / OTA v1. Run `dotnet test -c Release` for HTTP, USB, OTA, clock and STA UI regressions. See `RELEASE_1.2_RU.md` and `validation-1.2.0` for results; previous release folders remain historical. UI tests use private settings folders; a standalone smoke run can use `MONOPOD_DESKTOP_SETTINGS` (a folder) and `MONOPOD_SMOKE_OUTPUT` to isolate its files. `MONOPOD_UI_TEST_OUTPUT` enables test renders of the real firmware-update form using a simulated local device.

The NTP extension is in `..\MonoPod_v0.10.6.6`. Its host checks are `python ..\MonoPod_v0.10.6.6\tools\run_clock_native_tests.py --build-dir work\clock-native`. They compile the actual validation header, actual ClockService.cpp with host NVS/network/time adapters, and the original wheel tests with Arduino macro definitions. The runner does not change the PC clock or access hardware. Original OTA tests: `python -m pytest ..\MonoPod_v0.10.6.6\tests\test_ota_v1.py` (pytest is a development dependency only).

For S3 N16R8 firmware validation, copy the sketch into an isolated work folder, copy `tools/partitions/monopod_s3_n16r8_ota.csv` there as `partitions.csv`, then compile with `esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=custom`. No upload command is part of this workflow. The produced sketch application `.bin` is distinct from bootloader/partition/merged files. See the release report for the exact command and validated artifact; never use the historical build helper below for the new firmware.

The following additional Python tools and firmware build commands are **historical 1.0 development helpers pinned to firmware 0.10.0**, not the new 0.10.6 validation:

```powershell
python tools/run_native_tests.py
python tools/theme_crosscheck.py
```

Native checks require MSVC x64 Build Tools; script uses the installed VS2022 location on this machine. It copies current firmware SyncFiles sources into an isolated stub include environment and compiles actual Theme.cpp with its native SD adapter. Working files stay in `work/native`; logs/results go to docs. Theme crosscheck uses Pillow and the preserved legacy format module. It verifies byte-identical encoding for identical glyph input, and explicitly reports GDI/FreeType raster differences on the same TTF.

The original firmware pytest suite contains historical source-text assertions as well as functional tests. Read `VALIDATION_RU.md` before interpreting its result against the new protocol implementation.

Arduino CLI: installed Arduino-ESP32 3.3.12, ESP8266Audio, Adafruit GFX + ST7735, SdFat 2.x. ESP32-S3 N16R8:

```powershell
arduino-cli compile --jobs 4 --fqbn 'esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB' ..\MonoPod_v0.10.0\DIY_iPod_Mono
arduino-cli compile --jobs 2 --fqbn 'esp32:esp32:esp32:PartitionScheme=huge_app' ..\MonoPod_v0.10.0\DIY_iPod_Mono
```

Use the existing Arduino CLI config to locate user libraries. Bluetooth is compiled out for S3. Original ESP32 has no native USB CDC; use Wi-Fi. Do not flash S3 images onto classic ESP32. Flashing, partition changes, formatting and hardware validation were not performed as part of Desktop delivery.

Reference: [Microsoft SerialPort](https://learn.microsoft.com/en-us/dotnet/api/system.io.ports.serialport?view=dotnet-plat-ext-8.0). SerialPort BaseStream asynchronous operations do not inherit ReadTimeout; this implementation uses bounded synchronous reads on a worker instead.


Boot regression checks: `python tools/run_library_tests.py` and, after both board builds, `python tools/verify_firmware_stack.py`. The latter inspects actual Xtensa ELF prologues. Persistent-cache compilation helper: `tools/build_firmware.ps1 -Board s3 -Jobs 4` (or esp32); it never uploads firmware. See BOOT_REGRESSION_RU.md for exact crash evidence and measured build times.
