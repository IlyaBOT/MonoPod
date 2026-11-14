using System.Globalization;
using System.Text.RegularExpressions;

namespace MonoPod.Core;

public interface IClockTransport
{
    Task SetClockAsync(ClockConfiguration clock, CancellationToken ct);
}

public sealed record ClockConfiguration(long UnixSeconds, string Timezone, bool NtpEnabled, string NtpServer = "pool.ntp.org")
{
    public void Validate(bool customServerSupported)
    {
        if (UnixSeconds is < 1600000000 or > 4102444800)
            throw new ArgumentException("Clock time must be between September 2020 and January 2100.");
        if (string.IsNullOrEmpty(Timezone) || Timezone.Length > 63 ||
            !Regex.IsMatch(Timezone, @"\A[A-Za-z0-9+\-:,.</>]+\z"))
            throw new ArgumentException("Invalid device timezone.");
        ValidateNtpServer(NtpServer);
        if (!customServerSupported && NtpEnabled && !NtpServer.Equals("pool.ntp.org", StringComparison.OrdinalIgnoreCase))
            throw new NotSupportedException("This firmware uses built-in NTP servers. Install the NTP-server extension to select a different server.");
    }

    public static void ValidateNtpServer(string server)
    {
        if (string.IsNullOrEmpty(server) || server.Length > 253 || server.Split('.').Any(label =>
                label.Length is < 1 or > 63 || !Regex.IsMatch(label, @"\A[A-Za-z0-9](?:[A-Za-z0-9-]*[A-Za-z0-9])?\z")))
            throw new ArgumentException("Enter an NTP hostname or IPv4 address, without http://, a port or a path.");
    }
}

public static partial class PosixTimeZone
{
    // Windows names such as 'Russian Standard Time' are not understood by newlib.
    // Encode the current Windows adjustment rule as POSIX TZ, including DST.
    public static string FromWindows(TimeZoneInfo zone, DateTimeOffset now)
    {
        DateTime date = TimeZoneInfo.ConvertTime(now, zone).Date;
        var rule = zone.GetAdjustmentRules().LastOrDefault(r => r.DateStart <= date && r.DateEnd >= date);
        TimeSpan standard = zone.BaseUtcOffset + (rule?.BaseUtcOffsetDelta ?? TimeSpan.Zero);
        string result = "UTC" + Offset(-standard);
        if (zone.SupportsDaylightSavingTime && rule != null && rule.DaylightDelta != TimeSpan.Zero)
            result += "DST" + Offset(-(standard + rule.DaylightDelta)) + "," + Transition(rule.DaylightTransitionStart) + "," + Transition(rule.DaylightTransitionEnd);
        if (result.Length > 63) throw new NotSupportedException("The selected timezone rule is too long for this firmware.");
        return result;
    }

    static string Offset(TimeSpan value)
    {
        string sign = value < TimeSpan.Zero ? "-" : "";
        value = value.Duration();
        string hours = ((int)value.TotalHours).ToString(CultureInfo.InvariantCulture);
        return sign + hours + (value.Minutes != 0 || value.Seconds != 0 ? $":{value.Minutes:00}" : "") +
            (value.Seconds != 0 ? $":{value.Seconds:00}" : "");
    }

    static string Transition(TimeZoneInfo.TransitionTime transition)
    {
        string day;
        if (transition.IsFixedDateRule)
        {
            if (transition.Month == 2 && transition.Day == 29)
                throw new NotSupportedException("This timezone has a leap-day transition; choose a timezone with a recurring annual rule.");
            day = "J" + new DateTime(2001, transition.Month, transition.Day).DayOfYear;
        }
        else day = $"M{transition.Month}.{transition.Week}.{(int)transition.DayOfWeek}";
        return day + "/" + Offset(transition.TimeOfDay.TimeOfDay);
    }
}
