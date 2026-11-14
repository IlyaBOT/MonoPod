using System.Net;
using System.Windows.Forms;
using MonoPod.Core;
using MonoPod.Desktop.Forms;
using Xunit;

namespace MonoPod.Desktop.Tests;

public sealed partial class UiThemeRegressionTests
{
    [Theory]
    [InlineData(1f)]
    [InlineData(1.5f)]
    public Task DeviceClockShowsMoscowLocalTimeFromDeviceZoneInsteadOfUtcOrUnsavedSelection(float scale) => Sta(() =>
    {
        using var settings = new SettingsScope();
        settings.Preferences.ManualTimeZone = true;
        settings.Preferences.ManualTimeZoneId = "UTC"; // Not yet sent to the player.
        using var form = new MainForm(smoke:true, preferences:settings.Preferences);
        var instant = new DateTimeOffset(2026,11,1,15,50,0,TimeSpan.Zero);
        SetField(form,"device",new DeviceInfo { Firmware="0.10.6.9", ClockUnix=instant.ToUnixTimeSeconds(), Timezone="UTC-3", NtpEnabled=true });
        Invoke(form,"ShowClockInfo");
        string text = Field<Label>(form,"clockInfo").Text;
        Assert.Contains("Device time: 2026-11-01 18:50:00 (UTC+03:00)",text);
        Assert.DoesNotContain("UTC-3",text);
        Assert.Contains("NTP: on; server: pool.ntp.org",text);
        Field<TabControl>(form,"tabs").SelectedIndex=3;
        form.Opacity=0;form.ShowInTaskbar=false;form.Show();
        if(scale!=1f)form.Scale(new System.Drawing.SizeF(scale,scale));
        form.PerformLayout();
        string? output = Environment.GetEnvironmentVariable("MONOPOD_UI_TEST_OUTPUT");
        if(output!=null)
        {
            Directory.CreateDirectory(output);
            using var bitmap = new System.Drawing.Bitmap(form.Width,form.Height);
            form.DrawToBitmap(bitmap,new System.Drawing.Rectangle(System.Drawing.Point.Empty,form.Size));
            bitmap.Save(Path.Combine(output,$"clock-moscow-{(int)(scale*100)}.png"));
        }
        return Task.CompletedTask;
    });

    [Theory]
    [InlineData(0,"UTC-3","Device time: not set")]
    [InlineData(1793548200,"Unsupported zone","Device UTC time:")]
    public Task UnknownDeviceClockDoesNotPretendToUseTheSelectedZone(long unix, string zone, string expected) => Sta(() =>
    {
        using var settings = new SettingsScope();
        using var form = new MainForm(smoke:true,preferences:settings.Preferences);
        SetField(form,"device",new DeviceInfo { ClockUnix=unix,Timezone=zone });
        Invoke(form,"ShowClockInfo");
        Assert.Contains(expected,Field<Label>(form,"clockInfo").Text);
        return Task.CompletedTask;
    });

    [Theory]
    [InlineData(1f)]
    [InlineData(1.5f)]
    public Task FirmwareWizardKeepsUpdateAndCancelButtonsVisible(float scale) => Sta(async () =>
    {
        using var image=new FirmwareFixture();using var handler=new OtaHttpHandler();
        await using var wifi=new WiFiMonoPodTransport("127.0.0.1",handler);
        using var form=new FirmwareUpdateForm(wifi,default);
        // Invisible, taskbar-free window gives native controls real layout/paint
        // handles; no physical-device I/O or desktop input is used.
        form.Opacity=0;form.ShowInTaskbar=false;form.Show();
        for(int i=0;i<100 && (form.State==FirmwareUpdateState.Idle || Field<bool>(form,"active"));i++)await Task.Delay(10);
        Assert.Equal(FirmwareUpdateState.SelectingImage,form.State);
        var info=await FirmwareImage.InspectAsync(image.Path,handler.Info,default);
        SetField(form,"image",info);
        Field<TextBox>(form,"imagePath").Text="DIY_iPod_Mono.ino.bin";
        Field<Label>(form,"imageDetails").Text="Application: DIY_iPod_Mono, ESP32-S3, 64 KiB (simulated device for UI validation)";
        Field<TextBox>(form,"pin").Text="654321";Field<CheckBox>(form,"warning").Checked=true;
        Invoke(form,"SetState",FirmwareUpdateState.Ready,"Image checked. Enter the screen PIN and confirm Update firmware.");
        _=form.Handle;
        if(scale!=1f)form.Scale(new System.Drawing.SizeF(scale,scale));
        form.PerformLayout();
        var update=Field<Button>(form,"upload");var cancelButton=Field<Button>(form,"cancel");
        Assert.True(update.Enabled);Assert.True(cancelButton.Enabled);
        Assert.True(update.Bottom<=update.Parent!.ClientSize.Height);
        Assert.True(cancelButton.Right<=cancelButton.Parent!.ClientSize.Width);
        string? output=Environment.GetEnvironmentVariable("MONOPOD_UI_TEST_OUTPUT");
        if(output!=null)
        {
            Directory.CreateDirectory(output);
            using var bitmap=new System.Drawing.Bitmap(form.Width,form.Height);
            form.DrawToBitmap(bitmap,new System.Drawing.Rectangle(System.Drawing.Point.Empty,form.Size));
            bitmap.Save(Path.Combine(output,$"ota-ready-{(int)(scale*100)}.png"));
        }
    });

    [Fact]
    public Task ManualTimezoneSelectionChangesTheActualClockConfiguration() => Sta(() =>
    {
        using var settings = new SettingsScope();
        settings.Preferences.ManualTimeZoneId = "Removed Windows timezone";
        using var form = new MainForm(smoke:true,preferences:settings.Preferences);
        var zones = Field<ComboBox>(form,"timeZone");
        Assert.Equal(TimeZoneInfo.Local.Id,settings.Preferences.ManualTimeZoneId);
        Field<CheckBox>(form,"manualZone").Checked=true;
        Assert.True(zones.Enabled);
        zones.SelectedItem=zones.Items.Cast<TimeZoneInfo>().Single(zone=>zone.Id=="UTC");
        Assert.Equal("UTC0",settings.Preferences.Clock(DateTimeOffset.UtcNow).Timezone);
        Assert.True(File.Exists(Path.Combine(settings.Folder,"settings.json")));
        return Task.CompletedTask;
    });

    [Fact]
    public Task ReconnectionVerifiesDeviceIdAndReportsActualFirmwareChange() => Sta(async () =>
    {
        using var settings = new SettingsScope();
        settings.Preferences.PendingOta=new(){DeviceId="MP-TEST-S3",Board="esp32s3",FirmwareBefore="0.10.6.6",AcceptedByDevice=true};
        using var form = new MainForm(smoke:true,preferences:settings.Preferences);
        using var handler = new OtaHttpHandler();
        handler.Info=handler.Info with {Firmware="0.10.7"};handler.Device=handler.Device with {Firmware="0.10.7"};
        await using var wifi=new WiFiMonoPodTransport("127.0.0.1",handler);
        SetField(form,"transport",wifi);SetField(form,"device",handler.Device);
        await (Task)Invoke(form,"AfterConnection",CancellationToken.None)!;
        Assert.Null(settings.Preferences.PendingOta);
        Assert.Equal(FirmwareUpdateState.Verified,Field<FirmwareUpdateState>(form,"firmwareUpdateState"));
        Assert.Contains("0.10.6.6 → 0.10.7",Field<Label>(form,"firmwareInfo").Text);
    });

    [Fact]
    public Task ConnectionDoesNotAutomaticallyScanOrSetClockByDefault() => Sta(async () =>
    {
        using var settings = new SettingsScope();
        using var form = new MainForm(smoke: true, preferences: settings.Preferences);
        var target = new RecordingTransport();
        SetField(form, "transport", target); SetField(form, "device", target.Info);
        await (Task)Invoke(form, "AfterConnection", CancellationToken.None)!;
        Assert.Empty(target.Operations); Assert.Equal(0, target.ClockCalls); Assert.Equal(0, target.ScanCalls);
        Assert.False(Field<CheckBox>(form,"autoLibrary").Checked);
        Assert.False(Field<CheckBox>(form,"autoClock").Checked);
        Assert.False(Field<CheckBox>(form,"autoNtp").Checked);
        Assert.False(Field<CheckBox>(form,"manualZone").Checked);
        Assert.False(Field<ComboBox>(form,"timeZone").Enabled);
        Assert.IsType<TimeZoneInfo>(Field<ComboBox>(form,"timeZone").SelectedItem);
        Assert.Equal("pool.ntp.org", Field<TextBox>(form,"ntpServer").Text);
    });

    [Fact]
    public Task EnabledConnectionActionsSetClockAndReadLibraryAfterStartingSync() => Sta(async () =>
    {
        using var settings = new SettingsScope();
        settings.Preferences.AutoSyncLibraryOnConnect = true;
        settings.Preferences.AutoSyncTimeOnConnect = true;
        using var form = new MainForm(smoke: true, preferences: settings.Preferences);
        var target = new RecordingTransport();
        target.Info = target.Info with { Capabilities = ["list", "scan", "clock_sync"] };
        SetField(form, "transport", target); SetField(form, "device", target.Info);
        await (Task)Invoke(form,"AfterConnection",CancellationToken.None)!;
        Assert.True(target.StartCalls > 0); Assert.Equal(1,target.ClockCalls); Assert.Equal(1,target.ScanCalls);
        Assert.True(target.Operations.IndexOf("start") < target.Operations.IndexOf("clock"));
        Assert.True(target.Operations.IndexOf("scan") < target.Operations.IndexOf("list"));
    });

    [Fact]
    public Task ManualClockSyncRestoresPreviouslyInactiveUsbSession() => Sta(async () =>
    {
        using var settings = new SettingsScope(); settings.Preferences.AutoNtp = true;
        settings.Preferences.NtpServer = "ntp.example.org";
        using var form = new MainForm(smoke: true,preferences:settings.Preferences);
        var target = new UsbDevice { ClockSupported = true, CustomNtp = true };
        await using var usb = new UsbMonoPodTransport(() => target);
        var info = await usb.ConnectAsync(default);
        SetField(form,"transport",usb); SetField(form,"device",info);
        await (Task)Invoke(form,"SyncDeviceClock",CancellationToken.None,true)!;
        Assert.NotNull(target.Clock); Assert.Equal("ntp.example.org",target.Clock.NtpServer);
        Assert.True(target.Clock.NtpEnabled);
        Assert.InRange(Math.Abs(target.Clock.UnixSeconds-DateTimeOffset.UtcNow.ToUnixTimeSeconds()),0,5);
        Assert.False(target.Sync); Assert.False(target.Disposed);
        Assert.Equal("none",Field<DeviceInfo>(form,"device").SyncMode);
    });

    [Fact]
    public Task FirmwareUpdateLockDisablesFileThemeTimeAndConnectionControls() => Sta(async () =>
    {
        using var settings = new SettingsScope();
        using var form = new MainForm(smoke:true,preferences:settings.Preferences);
        using var handler = new OtaHttpHandler();
        await using var wifi = new WiFiMonoPodTransport("127.0.0.1",handler);
        SetField(form,"transport",wifi); SetField(form,"device",handler.Device);
        SetField(form,"busy",true); Invoke(form,"SetState");
        Assert.False(Field<Button>(form,"clockSyncButton").Enabled);
        Assert.False(Field<Button>(form,"disconnect").Enabled);
        Assert.False(Field<Button>(form,"start").Enabled);
        Assert.False(Field<TabControl>(form,"tabs").TabPages[3].Enabled);
        Assert.All(Field<List<(Control Control,string? Capability)>>(form,"commands"), c=>Assert.False(c.Control.Enabled));
        var firmware = form.MainMenuStrip!.Items.OfType<ToolStripMenuItem>().SelectMany(m=>m.DropDownItems.OfType<ToolStripMenuItem>()).Single(i=>i.Text=="Firmware Update…");
        Assert.False(firmware.Enabled);
        SetField(form,"busy",false); Invoke(form,"SetState"); Assert.True(firmware.Enabled);
    });

    [Fact]
    public Task FirmwareDialogShowsRealSlotAndOldFirmwareExplanation() => Sta(async () =>
    {
        using var handler = new OtaHttpHandler();
        await using var wifi = new WiFiMonoPodTransport("127.0.0.1",handler);
        using var form = new FirmwareUpdateForm(wifi,default);
        await (Task)Invoke(form,"CheckDevice")!;
        Assert.Equal(FirmwareUpdateState.SelectingImage,form.State);
        Assert.Contains("MP-TEST-S3",Field<Label>(form,"device").Text);
        Assert.False(Field<Button>(form,"upload").Enabled);
        handler.InfoStatus=HttpStatusCode.NotFound;
        await (Task)Invoke(form,"CheckDevice")!;
        Assert.Equal(FirmwareUpdateState.Error,form.State);
        Assert.Contains("first update through USB",form.OutcomeMessage);
        Assert.False(Field<Button>(form,"upload").Enabled);
    });

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public Task FirmwareDialogDoesNotLeaveAFalsePendingAttemptAfterPinRejection(bool accepted) => Sta(async () =>
    {
        using var settings=new SettingsScope(); using var image=new FirmwareFixture();
        using var handler=new OtaHttpHandler();
        if(!accepted){handler.UploadStatus=HttpStatusCode.Forbidden;handler.Acknowledgement="{\"ok\":false,\"error\":\"INVALID_PIN\"}";}
        await using var wifi=new WiFiMonoPodTransport("127.0.0.1",handler);
        using var form=new FirmwareUpdateForm(wifi,default);
        form.PendingChanged += p=>{settings.Preferences.PendingOta=p;settings.Preferences.Save();};
        SetField(form,"target",handler.Info);
        SetField(form,"image",await FirmwareImage.InspectAsync(image.Path,handler.Info,default));
        Field<TextBox>(form,"pin").Text="654321";
        await (Task)Invoke(form,"SendImage")!;
        await Task.Delay(25); // Flush delayed progress callbacks before checking final state.
        Assert.Equal("",Field<TextBox>(form,"pin").Text);
        Assert.Equal(accepted?FirmwareUpdateState.AwaitingUserReconnect:FirmwareUpdateState.Error,form.State);
        if(accepted){Assert.True(form.Pending!.AcceptedByDevice);Assert.Equal("MP-TEST-S3",settings.Preferences.PendingOta!.DeviceId);}
        else {Assert.Null(form.Pending);Assert.Null(settings.Preferences.PendingOta);}
        string saved=await File.ReadAllTextAsync(Path.Combine(settings.Folder,"settings.json"));
        Assert.DoesNotContain("654321",saved);
    });
}
