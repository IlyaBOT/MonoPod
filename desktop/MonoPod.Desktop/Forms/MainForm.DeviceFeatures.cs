using MonoPod.Core;

namespace MonoPod.Desktop.Forms;

public sealed partial class MainForm
{
    readonly CheckBox autoLibrary = new() { Text = "Automatically start Sync and load the music index on connection", AutoSize = true };
    readonly CheckBox autoClock = new() { Text = "Automatically synchronize device time with this PC on connection", AutoSize = true };
    readonly CheckBox autoNtp = new() { Text = "Device: automatically synchronize time over NTP when joining Wi-Fi", AutoSize = true };
    readonly CheckBox manualZone = new() { Text = "Select timezone manually (otherwise use the Windows timezone)", AutoSize = true };
    readonly TextBox ntpServer = new() { Width = 320, MaxLength = 253 };
    readonly ComboBox timeZone = new() { Width = 720, DropDownStyle = ComboBoxStyle.DropDownList };
    readonly Button clockSyncButton = new() { Text = "Apply clock settings and synchronize now", AutoSize = true };
    readonly Label clockInfo = new() { AutoSize = false, Width = 820, Height = 44, AutoEllipsis = true };
    readonly Label firmwareInfo = new() { AutoSize = false, Width = 820, Height = 50, AutoEllipsis = true,
        Text = "Firmware update: choose Device → Firmware Update while connected over Wi-Fi Sync." };
    FirmwareUpdateState firmwareUpdateState = FirmwareUpdateState.Idle;

    Control DeviceSettings()
    {
        var panel = new FlowLayoutPanel
        {
            AutoSize = true, FlowDirection = FlowDirection.TopDown, WrapContents = false,
            Padding = new(0, 12, 0, 12)
        };
        autoLibrary.Checked = prefs.AutoSyncLibraryOnConnect;
        autoClock.Checked = prefs.AutoSyncTimeOnConnect;
        autoNtp.Checked = prefs.AutoNtp;
        manualZone.Checked = prefs.ManualTimeZone;
        ntpServer.Text = prefs.NtpServer;
        var zones = TimeZoneInfo.GetSystemTimeZones().ToArray();
        timeZone.Items.AddRange(zones.Cast<object>().ToArray());
        timeZone.DisplayMember = nameof(TimeZoneInfo.DisplayName);
        timeZone.SelectedItem = zones.FirstOrDefault(zone => zone.Id == prefs.ManualTimeZoneId)
            ?? zones.First(zone => zone.Id == TimeZoneInfo.Local.Id);
        prefs.ManualTimeZoneId = ((TimeZoneInfo)timeZone.SelectedItem!).Id;
        timeZone.Enabled = manualZone.Checked;
        autoLibrary.CheckedChanged += (_, _) => { prefs.AutoSyncLibraryOnConnect = autoLibrary.Checked; prefs.Save(); };
        autoClock.CheckedChanged += (_, _) => { prefs.AutoSyncTimeOnConnect = autoClock.Checked; prefs.Save(); };
        autoNtp.CheckedChanged += (_, _) => { prefs.AutoNtp = autoNtp.Checked; prefs.Save(); };
        manualZone.CheckedChanged += (_, _) => { prefs.ManualTimeZone = manualZone.Checked; timeZone.Enabled = manualZone.Checked; prefs.Save(); };
        timeZone.SelectedIndexChanged += (_, _) =>
        {
            if (timeZone.SelectedItem is TimeZoneInfo selected) { prefs.ManualTimeZoneId = selected.Id; prefs.Save(); }
        };
        ntpServer.TextChanged += (_, _) => { prefs.NtpServer = ntpServer.Text.Trim(); prefs.Save(); };
        clockSyncButton.Click += (_, _) => _ = Run("Synchronizing device time", ct => SyncDeviceClock(ct));
        panel.Controls.AddRange([
            autoLibrary, autoClock, autoNtp,
            new Label { Text = "NTP server (default: pool.ntp.org)", AutoSize = true }, ntpServer,
            manualZone, timeZone,
            new Label { Text = "Automatic timezone: " + TimeZoneInfo.Local.DisplayName, AutoSize = true },
            clockSyncButton, clockInfo, firmwareInfo,
            new Label { AutoSize = false, Width = 820, Height = 44,
                Text = "Apply saves NTP and timezone on MonoPod. NTP then works without Desktop in a connected Wi-Fi network.\r\nAutomatic connection actions are off by default. Wi-Fi access-point mode alone has no Internet NTP." }
        ]);
        return panel;
    }

    void ShowClockInfo()
    {
        if (device == null) { clockInfo.Text = "Connect to read the device clock."; return; }
        string value = "Device time: not set";
        if (device.ClockUnix is >= 1600000000 and <= 4102444800)
        {
            var instant = DateTimeOffset.FromUnixTimeSeconds(device.ClockUnix);
            value = PosixTimeZone.TryGetLocalTime(device.Timezone, instant, out var local, out var offset)
                ? $"Device time: {local:yyyy-MM-dd HH:mm:ss} ({PosixTimeZone.FormatUtcOffset(offset)})"
                : $"Device UTC time: {instant:yyyy-MM-dd HH:mm:ss}; timezone unavailable — apply clock settings.";
        }
        clockInfo.Text = $"{value}\r\nNTP: {(device.NtpEnabled ? "on" : "off")}; server: {device.NtpServer}";
    }

    async Task SyncDeviceClock(CancellationToken ct, bool setPcTime = true)
    {
        Require();
        if (transport is not IClockTransport clock || device?.Supports("clock_sync") != true)
            throw new NotSupportedException("This firmware does not advertise clock synchronization.");
        bool temporaryUsbSync = transport.IsUsb && device.SyncMode != "usb";
        try
        {
            await EnsureSync(ct);
            long? preserve = !setPcTime && device.ClockUnix is >= 1600000000 and <= 4102444800 ? device.ClockUnix : null;
            var value = prefs.Clock(DateTimeOffset.UtcNow, preserve);
            await clock.SetClockAsync(value, ct);
            device = await transport.GetDeviceInfoAsync(ct);
            ShowInfo();
            log.Write(setPcTime ? "Device time synchronized with PC; timezone/NTP settings saved." : "Device NTP and timezone settings saved.");
        }
        finally
        {
            if (temporaryUsbSync && transport is UsbMonoPodTransport { NeedsRecovery: true })
                device = device! with { SyncMode = "none" };
            if (temporaryUsbSync && transport is UsbMonoPodTransport { NeedsRecovery: false })
            {
                using var cleanup = new CancellationTokenSource(TimeSpan.FromSeconds(90));
                await transport.EndSyncAsync(cleanup.Token);
                device = await transport.GetDeviceInfoAsync(cleanup.Token);
                ShowInfo();
            }
        }
    }

    async Task AfterConnection(CancellationToken ct)
    {
        if (transport is WiFiMonoPodTransport wifi && prefs.PendingOta is { } pending)
        {
            firmwareUpdateState = FirmwareUpdateState.CheckingDevice;
            OtaReconnectionResult result;
            try { result = OtaReconnectionResult.Check(pending, device!, await wifi.GetOtaInfoAsync(ct)); }
            catch (Exception error)
            {
                firmwareUpdateState = FirmwareUpdateState.Error;
                firmwareInfo.Text = "The previous firmware update could not be verified: " + error.Message;
                throw;
            }
            firmwareUpdateState = result.SameDevice && result.VersionChanged ? FirmwareUpdateState.Verified : FirmwareUpdateState.Error;
            firmwareInfo.Text = result.Message;
            log.Write(result.Message);
            if (!smoke) MessageBox.Show(this, result.Message, "Firmware verification", MessageBoxButtons.OK,
                result.SameDevice ? MessageBoxIcon.Information : MessageBoxIcon.Warning);
            if (result.SameDevice) { prefs.PendingOta = null; prefs.Save(); }
            else return;
        }
        if (prefs.AutoSyncLibraryOnConnect) await EnsureSync(ct);
        if ((prefs.AutoSyncTimeOnConnect || prefs.AutoNtp) && device?.Supports("clock_sync") == true)
            await SyncDeviceClock(ct, prefs.AutoSyncTimeOnConnect);
        if (prefs.AutoSyncLibraryOnConnect && device?.Supports("list") == true)
        {
            if (!device.Supports("library_db_v1") && device.Supports("scan")) await transport!.ScanAsync(ct);
            device = await transport!.GetDeviceInfoAsync(ct);
            ShowInfo();
            await ReadMusicLibrary(ct);
        }
    }

    async Task UpdateFirmware()
    {
        if (busy || transport is not WiFiMonoPodTransport wifi || device?.SyncMode != "wifi") return;
        bool awaitReconnect = false;
        await Run("Firmware update", async ct =>
        {
            using var dialog = new FirmwareUpdateForm(wifi, ct);
            dialog.PendingChanged += pending =>
            {
                prefs.PendingOta = pending;
                try { prefs.Save(); }
                catch (IOException error) { log.Write("Could not save firmware verification state. Keep Desktop open to verify after reconnect: " + error.Message); }
                catch (UnauthorizedAccessException error) { log.Write("Could not save firmware verification state: " + error.Message); }
            };
            dialog.StateChanged += (state,message) => { firmwareUpdateState = state; firmwareInfo.Text = message; };
            dialog.ShowDialog(this);
            if (dialog.Pending != null)
            {
                awaitReconnect = true;
                await CloseTransport(); // Do not send END to an updating/rebooting device.
            }
            log.Write(dialog.OutcomeMessage);
        });
        if (awaitReconnect) status.Text = "Reopen Wi-Fi Sync on MonoPod, then Connect to verify its running version.";
    }
}
