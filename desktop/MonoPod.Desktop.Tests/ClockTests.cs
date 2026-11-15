using System.Text.Json;
using MonoPod.Core;
using MonoPod.Desktop.Services;
using Xunit;

namespace MonoPod.Desktop.Tests;

public sealed class ClockTests
{
    [Theory]
    [InlineData("pool.ntp.org")]
    [InlineData("time.google.com")]
    [InlineData("192.168.1.1")]
    [InlineData("ntp.local")]
    public void HostnameAndIpv4AreAccepted(string server) => ClockConfiguration.ValidateNtpServer(server);

    [Theory]
    [InlineData("")]
    [InlineData("http://pool.ntp.org")]
    [InlineData("pool.ntp.org:123")]
    [InlineData("one..two")]
    [InlineData("-host.test")]
    [InlineData("host-.test")]
    [InlineData("ntp.test/path")]
    [InlineData("ntp.test\r\n")]
    public void InvalidServerIsRejected(string server) => Assert.Throws<ArgumentException>(() => ClockConfiguration.ValidateNtpServer(server));

    [Fact]
    public void DefaultsAndOldPreferencesKeepAllAutomaticActionsOff()
    {
        var prefs = JsonSerializer.Deserialize<Preferences>("{\"Transport\":\"USB\",\"Port\":\"COM18\"}")!;
        Assert.False(prefs.AutoSyncLibraryOnConnect); Assert.False(prefs.AutoSyncTimeOnConnect);
        Assert.False(prefs.AutoNtp); Assert.False(prefs.ManualTimeZone);
        Assert.Equal("pool.ntp.org", prefs.NtpServer);
        Assert.Equal("COM18", prefs.Port);
        var now = DateTimeOffset.UtcNow;
        Assert.Equal(now.ToUnixTimeSeconds(), prefs.Clock(now).UnixSeconds);
        Assert.Equal(PosixTimeZone.FromWindows(TimeZoneInfo.Local, now), prefs.Clock(now).Timezone);
    }

    [Theory]
    [InlineData(180, "UTC-3")]
    [InlineData(330, "UTC-5:30")]
    [InlineData(-210, "UTC3:30")]
    [InlineData(0, "UTC0")]
    public void PosixOffsetsUseTheOppositeSignToWindows(int minutes, string expected)
    {
        var zone = TimeZoneInfo.CreateCustomTimeZone("Test", TimeSpan.FromMinutes(minutes), "Test", "Test");
        Assert.Equal(expected, PosixTimeZone.FromWindows(zone, DateTimeOffset.UtcNow));
    }

    [Fact]
    public void DaylightTransitionsArePreservedInsteadOfFreezingCurrentOffset()
    {
        var start = TimeZoneInfo.TransitionTime.CreateFloatingDateRule(new DateTime(1, 1, 1, 2, 0, 0), 3, 2, DayOfWeek.Sunday);
        var end = TimeZoneInfo.TransitionTime.CreateFloatingDateRule(new DateTime(1, 1, 1, 2, 0, 0), 11, 1, DayOfWeek.Sunday);
        var rule = TimeZoneInfo.AdjustmentRule.CreateAdjustmentRule(new DateTime(2020, 1, 1), new DateTime(2040, 12, 31), TimeSpan.FromHours(1), start, end);
        var zone = TimeZoneInfo.CreateCustomTimeZone("Test Eastern", TimeSpan.FromHours(-5), "Test", "STD", "DST", [rule]);
        Assert.Equal("UTC5DST4,M3.2.0/2,M11.1.0/2", PosixTimeZone.FromWindows(zone, new DateTimeOffset(2026, 7, 1, 0, 0, 0, TimeSpan.Zero)));
    }

    [Fact]
    public async Task HttpClockUsesFormFieldsAndAdvertisedServerCapability()
    {
        using var handler = new OtaHttpHandler();
        await using var wifi = new WiFiMonoPodTransport("127.0.0.1", handler);
        await wifi.ConnectAsync(default);
        await wifi.SetClockAsync(new(1793500000, "UTC-3", true, "ntp.example.org"), default);
        Assert.Contains("unix=1793500000", handler.ClockBody);
        Assert.Contains("timezone=UTC-3", handler.ClockBody);
        Assert.Contains("ntp=1", handler.ClockBody);
        Assert.Contains("ntp_server=ntp.example.org", handler.ClockBody);
    }

    [Fact]
    public async Task OldFirmwareReceivesOriginalClockProtocolWithoutPretendServerSupport()
    {
        using var handler = new OtaHttpHandler();
        handler.Device = handler.Device with { Capabilities = ["clock_sync", "ntp"] };
        await using var wifi = new WiFiMonoPodTransport("127.0.0.1", handler);
        await wifi.ConnectAsync(default);
        await wifi.SetClockAsync(new(1793500000, "UTC-3", true), default);
        Assert.DoesNotContain("ntp_server", handler.ClockBody);
        await Assert.ThrowsAsync<NotSupportedException>(() => wifi.SetClockAsync(new(1793500000, "UTC-3", true, "ntp.example.org"), default));
        Assert.Single(handler.Routes, r => r == "/api/clock");
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public async Task UsbClockKeepsMpOdV4FramingAndOptionalServerIsHexEncoded(bool extended)
    {
        var device = new UsbDevice { ClockSupported = true, CustomNtp = extended };
        await using var usb = new UsbMonoPodTransport(() => device);
        await usb.ConnectAsync(default); await usb.StartSyncAsync(default);
        var value = new ClockConfiguration(1793500000, "UTC-3", true, extended ? "ntp.example.org" : "pool.ntp.org");
        await usb.SetClockAsync(value, default);
        Assert.Equal(value, device.Clock);
        string command = Assert.Single(device.Commands, c => c.StartsWith("MPOD1 X CLOCK "));
        Assert.Contains("1793500000 5554432D33 1", command);
        Assert.Equal(extended ? 7 : 6, command.Split(' ').Length);
        await usb.EndSyncAsync(default);
    }

    [Fact]
    public async Task InvalidClockDataIsNotSentAndDoesNotBreakUsbConnection()
    {
        var device = new UsbDevice { ClockSupported = true, CustomNtp = true };
        await using var usb = new UsbMonoPodTransport(() => device);
        await usb.ConnectAsync(default); await usb.StartSyncAsync(default);
        await Assert.ThrowsAsync<ArgumentException>(() => usb.SetClockAsync(new(0, "UTC0", false), default));
        Assert.DoesNotContain(device.Commands, c => c.StartsWith("MPOD1 X CLOCK "));
        Assert.Equal(4, (await usb.GetDeviceInfoAsync(default)).Protocol);
        await usb.EndSyncAsync(default);
    }
}
