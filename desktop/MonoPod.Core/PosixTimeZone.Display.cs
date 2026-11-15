using System.Globalization;
using System.Text.RegularExpressions;

namespace MonoPod.Core;

public static partial class PosixTimeZone
{
    const string ZoneName = @"(?:[A-Za-z]{3,}|<[A-Za-z0-9+\-]{3,}>)";
    const string ZoneOffset = @"[+\-]?[0-9]{1,2}(?::[0-9]{1,2}(?::[0-9]{1,2})?)?";
    const string Rule = @"(?:M[0-9]{1,2}\.[1-5]\.[0-6]|J?[0-9]{1,3})(?:/[+\-]?[0-9]{1,3}(?::[0-9]{1,2}(?::[0-9]{1,2})?)?)?";
    static readonly Regex DeviceZone = new(@"\A" + ZoneName + "(?<standard>" + ZoneOffset + ")(?:(?<dst>" +
        ZoneName + ")(?<daylight>" + ZoneOffset + ")?,(?<start>" + Rule + "),(?<end>" + Rule + @"))?\z",
        RegexOptions.CultureInvariant | RegexOptions.ExplicitCapture);

    // Decode the device's actual POSIX TZ, independently of Windows or unsaved
    // UI selections. A POSIX offset is added to local time to obtain UTC.
    public static bool TryGetLocalTime(string? timezone, DateTimeOffset instant, out DateTime local, out TimeSpan utcOffset)
    {
        local = default; utcOffset = default;
        if (string.IsNullOrEmpty(timezone) || timezone.Length > 63) return false;
        var match = DeviceZone.Match(timezone);
        if (!match.Success || !TryDuration(match.Groups["standard"].Value, 24, out var west)) return false;
        TimeSpan standard = -west;
        utcOffset = standard;
        if (match.Groups["dst"].Success)
        {
            TimeSpan daylight = standard + TimeSpan.FromHours(1);
            if (match.Groups["daylight"].Success)
            {
                if (!TryDuration(match.Groups["daylight"].Value, 24, out west)) return false;
                daylight = -west;
            }
            DateTime utc = instant.UtcDateTime, latest = DateTime.MinValue;
            // Examine neighbouring years as southern-hemisphere DST crosses
            // New Year, and rule times can extend beyond a calendar day.
            for (int year = Math.Max(2, utc.Year - 1); year <= Math.Min(9998, utc.Year + 1); year++)
            {
                if (!TryTransition(match.Groups["start"].Value, year, out var start) ||
                    !TryTransition(match.Groups["end"].Value, year, out var end)) return false;
                start -= standard; end -= daylight;
                if (start <= utc && start >= latest) { latest = start; utcOffset = daylight; }
                if (end <= utc && end >= latest) { latest = end; utcOffset = standard; }
            }
        }
        try { local = DateTime.SpecifyKind(instant.UtcDateTime + utcOffset, DateTimeKind.Unspecified); return true; }
        catch (ArgumentOutOfRangeException) { return false; }
    }

    public static string FormatUtcOffset(TimeSpan offset)
    {
        string sign = offset < TimeSpan.Zero ? "-" : "+";
        offset = offset.Duration();
        return "UTC" + sign + ((int)offset.TotalHours).ToString("00", CultureInfo.InvariantCulture) +
            ":" + offset.Minutes.ToString("00", CultureInfo.InvariantCulture) +
            (offset.Seconds == 0 ? "" : ":" + offset.Seconds.ToString("00", CultureInfo.InvariantCulture));
    }

    static bool TryDuration(string text, int maxHours, out TimeSpan value)
    {
        value = default;
        bool negative = text.StartsWith('-');
        string[] parts = text.TrimStart('+', '-').Split(':');
        if (parts.Length is < 1 or > 3 || !int.TryParse(parts[0], NumberStyles.None, CultureInfo.InvariantCulture, out int hours) || hours > maxHours)
            return false;
        int minutes = 0, seconds = 0;
        if (parts.Length > 1 && (!int.TryParse(parts[1], NumberStyles.None, CultureInfo.InvariantCulture, out minutes) || minutes > 59)) return false;
        if (parts.Length > 2 && (!int.TryParse(parts[2], NumberStyles.None, CultureInfo.InvariantCulture, out seconds) || seconds > 59)) return false;
        value = TimeSpan.FromSeconds((hours * 3600 + minutes * 60 + seconds) * (negative ? -1 : 1));
        return true;
    }

    static bool TryTransition(string text, int year, out DateTime transition)
    {
        transition = default;
        string[] parts = text.Split('/');
        TimeSpan time = TimeSpan.FromHours(2);
        if (parts.Length > 1 && !TryDuration(parts[1], 167, out time)) return false;
        string day = parts[0];
        DateTime date;
        if (day.StartsWith('M'))
        {
            string[] fields = day[1..].Split('.');
            int month = int.Parse(fields[0], CultureInfo.InvariantCulture), week = int.Parse(fields[1], CultureInfo.InvariantCulture), weekday = int.Parse(fields[2], CultureInfo.InvariantCulture);
            if (month is < 1 or > 12) return false;
            date = new DateTime(year, month, 1);
            int number = 1 + (weekday - (int)date.DayOfWeek + 7) % 7 + (week - 1) * 7;
            if (number > DateTime.DaysInMonth(year, month)) number -= 7;
            date = new DateTime(year, month, number);
        }
        else
        {
            bool julian = day.StartsWith('J');
            int number = int.Parse(julian ? day[1..] : day, CultureInfo.InvariantCulture);
            if (number < (julian ? 1 : 0) || number > 365) return false;
            int index = julian ? number - 1 + (number >= 60 && DateTime.IsLeapYear(year) ? 1 : 0) : number;
            date = new DateTime(year, 1, 1).AddDays(index);
        }
        transition = date + time;
        return true;
    }
}
