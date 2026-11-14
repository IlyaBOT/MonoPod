The supplied project archive contains firmware only, not MonoPod Desktop's Visual Studio solution.
This folder therefore contains *integration-ready* Windows Forms code, not a finished or tested MonoPod Desktop build.

WinForms:
- Add MonoPodClockSettingsControl.cs and MonoPodClockTransport.cs to the existing Windows Forms project.
- Place MonoPodClockSettingsControl on Settings tab and assign SyncToDeviceAsync.
- For Wi-Fi call MonoPodClockTransport.WiFiAsync(http, deviceUri, settings, ct).
- For USB call MonoPodClockTransport.UsbCommand(settings) and send it over the already-open MPOD1 v4 command channel between SYNC START / SYNC END; expect MPOD1 OK CLOCK_SET.
- POSIX timezone e.g. UTC0; CET-1CEST,M3.5.0,M10.5.0/3 (DST). NTP supplies UTC, not zone.
- Always send PC current Unix time when pressing Sync; NTP automatically refreshes if enabled and STA has Internet.
