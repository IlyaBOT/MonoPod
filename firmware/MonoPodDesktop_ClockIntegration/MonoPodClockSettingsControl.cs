// MonoPod Desktop integration control. Add to your existing WinForms project.
// Actual application sources were not supplied with the firmware archive.
using System;
using System.Drawing;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
namespace MonoPod.Desktop;

public sealed record MonoPodClockSettings(long UnixSeconds, string PosixTimeZone, bool NtpEnabled);

public sealed class MonoPodClockSettingsControl : UserControl
{
    public CheckBox AutoNtp { get; } = new()
    {
        Text = "Synchronize via NTP when connected to Wi-Fi", AutoSize = true,
        Checked = false, Left = 10, Top = 10
    };
    public TextBox TimeZone { get; } = new()
    {
        Text = "UTC0", Width = 175, Left = 130, Top = 46
    };
    public Button Synchronize { get; } = new()
    {
        Text = "Sync clock with PC", AutoSize = true, Left = 10, Top = 88
    };
    public Func<MonoPodClockSettings, CancellationToken, Task>? SyncToDeviceAsync { get; set; }
    public MonoPodClockSettingsControl()
    {
        Size = new Size(400, 130);
        Controls.Add(AutoNtp);
        Controls.Add(new Label { Text = "POSIX time zone:", AutoSize = true, Left = 10, Top = 49 });
        Controls.Add(TimeZone);
        Controls.Add(Synchronize);
        Synchronize.Click += async (_, _) => await PushAsync();
    }
    public async Task PushAsync(CancellationToken ct = default)
    {
        if (SyncToDeviceAsync is null)
        {
            MessageBox.Show(this, "Connect MonoPod via USB or Wi-Fi first.");
            return;
        }
        string tz = TimeZone.Text.Trim();
        if (tz.Length is 0 or > 63 || !System.Text.RegularExpressions.Regex.IsMatch(tz,@"^[A-Za-z0-9+\-:,.<>/]+$"))
        {
            MessageBox.Show(this,"Enter a valid POSIX TZ string, e.g. UTC0, CET-1CEST,M3.5.0,M10.5.0/3.");
            return;
        }
        Synchronize.Enabled = false;
        try
        {
            var settings = new MonoPodClockSettings(DateTimeOffset.UtcNow.ToUnixTimeSeconds(),tz,AutoNtp.Checked);
            await SyncToDeviceAsync(settings,ct);
        }
        catch (Exception ex) { MessageBox.Show(this,$"Time sync failed: {ex.Message}"); }
        finally { Synchronize.Enabled = true; }
    }
}
