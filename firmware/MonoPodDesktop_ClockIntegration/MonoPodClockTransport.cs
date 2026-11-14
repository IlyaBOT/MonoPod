using System;
using System.Collections.Generic;
using System.Net.Http;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
namespace MonoPod.Desktop;
public static class MonoPodClockTransport
{
    // Call only while Wi-Fi Sync mode is active. URL is the current MonoPod LAN IP.
    public static async Task WiFiAsync(HttpClient http,Uri baseUri,MonoPodClockSettings clock,CancellationToken ct=default)
    {
        using var content = new FormUrlEncodedContent(new Dictionary<string,string>
        {
            ["unix"]=clock.UnixSeconds.ToString(System.Globalization.CultureInfo.InvariantCulture),
            ["timezone"]=clock.PosixTimeZone,
            ["ntp"]=clock.NtpEnabled?"1":"0"
        });
        using var response=await http.PostAsync(new Uri(baseUri,"/api/clock"),content,ct);
        response.EnsureSuccessStatusCode();
    }
    // Existing MPOD1 v4 USB transport should pass this line to its command channel
    // AFTER HELLO and SYNC START, and require a reply of 'MPOD1 OK CLOCK_SET'.
    public static string UsbCommand(MonoPodClockSettings clock)
    {
        string tzHex=Convert.ToHexString(Encoding.UTF8.GetBytes(clock.PosixTimeZone));
        return $"MPOD1 X CLOCK {clock.UnixSeconds} {tzHex} {(clock.NtpEnabled?1:0)}\n";
    }
}
