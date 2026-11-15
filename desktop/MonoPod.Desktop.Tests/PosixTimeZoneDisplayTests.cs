using System.Globalization;
using MonoPod.Core;
using MonoPod.Desktop.Services;
using Xunit;

namespace MonoPod.Desktop.Tests;

public sealed class PosixTimeZoneDisplayTests
{
    [Theory]
    [InlineData("UTC-3", "2026-11-01 18:50:00", "UTC+03:00")]
    [InlineData("MSK-3", "2026-11-01 18:50:00", "UTC+03:00")]
    [InlineData("UTC3", "2026-11-01 12:50:00", "UTC-03:00")]
    [InlineData("UTC0", "2026-11-01 15:50:00", "UTC+00:00")]
    [InlineData("UTC-5:30", "2026-11-01 21:20:00", "UTC+05:30")]
    [InlineData("UTC3:30", "2026-11-01 12:20:00", "UTC-03:30")]
    [InlineData("<+0545>-5:45", "2026-11-01 21:35:00", "UTC+05:45")]
    [InlineData("ABC-3:15:30", "2026-11-01 19:05:30", "UTC+03:15:30")]
    public void DevicePosixZoneIsDisplayedWithCivilOffset(string zone, string expectedTime, string expectedOffset)
    {
        var instant = new DateTimeOffset(2026, 11, 1, 15, 50, 0, TimeSpan.Zero);
        Assert.True(PosixTimeZone.TryGetLocalTime(zone, instant, out var local, out var offset));
        Assert.Equal(expectedTime, local.ToString("yyyy-MM-dd HH:mm:ss", CultureInfo.InvariantCulture));
        Assert.Equal(DateTimeKind.Unspecified, local.Kind);
        Assert.Equal(expectedOffset, PosixTimeZone.FormatUtcOffset(offset));
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void MoscowSyncPreservesTheUtcInstantAndOnlyAppliesTheOffsetWhenDisplaying(bool manual)
    {
        var now = new DateTimeOffset(2026, 11, 1, 18, 50, 0, TimeSpan.FromHours(3));
        var zone = TimeZoneInfo.FindSystemTimeZoneById("Russian Standard Time");
        var prefs = new Preferences { ManualTimeZone = manual, ManualTimeZoneId = zone.Id };
        var value = manual ? prefs.Clock(now) : new ClockConfiguration(now.ToUnixTimeSeconds(), PosixTimeZone.FromWindows(zone, now), false);
        Assert.Equal(now.UtcDateTime, DateTimeOffset.FromUnixTimeSeconds(value.UnixSeconds).UtcDateTime);
        Assert.Equal("UTC-3", value.Timezone);
        Assert.True(PosixTimeZone.TryGetLocalTime(value.Timezone, DateTimeOffset.FromUnixTimeSeconds(value.UnixSeconds), out var local, out var offset));
        Assert.Equal(now.DateTime, local);
        Assert.Equal(TimeSpan.FromHours(3), offset);
    }

    [Theory]
    [InlineData("UTC5DST4,M3.2.0/2,M11.1.0/2", "2026-03-08T06:59:59Z", -300)]
    [InlineData("UTC5DST4,M3.2.0/2,M11.1.0/2", "2026-03-08T07:00:00Z", -240)]
    [InlineData("UTC5DST4,M3.2.0/2,M11.1.0/2", "2026-11-01T05:59:59Z", -240)]
    [InlineData("UTC5DST4,M3.2.0/2,M11.1.0/2", "2026-11-01T06:00:00Z", -300)]
    [InlineData("EST5EDT,M3.2.0,M11.1.0", "2026-07-01T00:00:00Z", -240)]
    [InlineData("AEST-10AEDT-11,M10.1.0/2,M4.1.0/3", "2026-01-01T00:00:00Z", 660)]
    [InlineData("AEST-10AEDT-11,M10.1.0/2,M4.1.0/3", "2026-07-01T00:00:00Z", 600)]
    [InlineData("UTC0DST-1,J60/0,J300/0", "2028-02-29T12:00:00Z", 0)]
    [InlineData("UTC0DST-1,J60/0,J300/0", "2028-03-01T00:00:00Z", 60)]
    [InlineData("UTC0DST-1,59/0,300/0", "2028-02-29T00:00:00Z", 60)]
    [InlineData("UTC-1DST-2,M3.5.0/2,M10.5.0/3", "2026-03-29T00:59:59Z", 60)]
    [InlineData("UTC-1DST-2,M3.5.0/2,M10.5.0/3", "2026-03-29T01:00:00Z", 120)]
    public void DaylightRulesAreEvaluatedAtTheDeviceInstant(string zone, string time, int offsetMinutes)
    {
        var instant = DateTimeOffset.Parse(time, CultureInfo.InvariantCulture);
        Assert.True(PosixTimeZone.TryGetLocalTime(zone, instant, out var local, out var offset));
        Assert.Equal(TimeSpan.FromMinutes(offsetMinutes), offset);
        Assert.Equal(instant.UtcDateTime.AddMinutes(offsetMinutes), local);
    }

    [Theory]
    [InlineData("Russian Standard Time")]
    [InlineData("Eastern Standard Time")]
    [InlineData("W. Europe Standard Time")]
    [InlineData("Cen. Australia Standard Time")]
    [InlineData("Chatham Islands Standard Time")]
    [InlineData("Newfoundland Standard Time")]
    public void WindowsRulesRoundTripAcrossSeasonsAndYearBoundaries(string id)
    {
        var zone = TimeZoneInfo.FindSystemTimeZoneById(id);
        foreach (int year in new[] { 2026, 2028 })
        foreach (int month in new[] { 1, 3, 4, 7, 10, 11, 12 })
        {
            var instant = new DateTimeOffset(year, month, 1, 0, 0, 0, TimeSpan.Zero);
            string posix = PosixTimeZone.FromWindows(zone, instant);
            Assert.True(PosixTimeZone.TryGetLocalTime(posix, instant, out var local, out var offset), posix);
            Assert.Equal(zone.GetUtcOffset(instant), offset);
            Assert.Equal(TimeZoneInfo.ConvertTime(instant, zone).DateTime, local);
        }
    }

    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("Russian Standard Time")]
    [InlineData("UTC-3junk")]
    [InlineData("UTC-3\r\n")]
    [InlineData("UTC25")]
    [InlineData("UTC-3:60")]
    [InlineData("UTC-3:00:60")]
    [InlineData("UTC0DST-1,M13.1.0,M10.1.0")]
    [InlineData("UTC0DST-1,J0,J300")]
    [InlineData("UTC0DST-1,J60,J366")]
    [InlineData("UTC0DST-1,M3.1.0/168,M10.1.0")]
    [InlineData("EST5EDT")]
    public void UnknownOrMalformedZoneDoesNotBecomeAnInventedLocalTime(string? zone) =>
        Assert.False(PosixTimeZone.TryGetLocalTime(zone, DateTimeOffset.UtcNow, out _, out _));
}
