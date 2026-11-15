using System.Text.Json;

namespace MonoPod.Desktop.Services;
public sealed class Preferences
{
    readonly string? storageFolder;
    public Preferences() { }
    // Per-instance location keeps previews/tests independent of the user's profile.
    public Preferences(string storageFolder)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(storageFolder);
        this.storageFolder = Path.GetFullPath(storageFolder);
    }
    public string Transport { get; set; } = "USB";
    public string Port { get; set; } = "";
    public string Address { get; set; } = "";
    public List<string> Addresses { get; set; } = [];
    public bool WarnUsb { get; set; } = true;
    public bool AutoSyncLibraryOnConnect { get; set; }
    public bool AutoSyncTimeOnConnect { get; set; }
    public bool AutoNtp { get; set; }
    public string NtpServer { get; set; } = "pool.ntp.org";
    public bool ManualTimeZone { get; set; }
    public string ManualTimeZoneId { get; set; } = TimeZoneInfo.Local.Id;
    public MonoPod.Core.PendingOtaVerification? PendingOta { get; set; }
    public int Tab { get; set; }
    public string LocalFolder { get; set; } = "";
    public Dictionary<string, int> ColumnWidths { get; set; } = [];
    public Themes.ThemeProfile Theme { get; set; } = new();
    public static string Folder => Environment.GetEnvironmentVariable("MONOPOD_DESKTOP_SETTINGS") ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "MonoPod", "Desktop");
    public static string FileName => Path.Combine(Folder, "settings.json");
    internal string StoreFileName => Path.Combine(storageFolder ?? Folder, "settings.json");

    public static Preferences Load()
    {
        try
        {
            var result = JsonSerializer.Deserialize<Preferences>(File.ReadAllText(FileName)) ?? new();
            result.Theme ??= new();
            if (result.Theme.Colors is null || result.Theme.Colors.Length != 12)
                result.Theme.Colors = new MonoPod.Core.ThemeDocument().Colors;
            result.Addresses ??= [];
            result.ColumnWidths ??= [];
            result.Transport ??= "USB";
            result.Port ??= "";
            result.Address ??= "";
            result.LocalFolder ??= "";
            result.NtpServer ??= "pool.ntp.org";
            result.ManualTimeZoneId ??= TimeZoneInfo.Local.Id;
            return result;
        }
        catch (IOException)
        {
            return new();
        }
        catch (JsonException)
        {
            return new();
        }
    }

    public void Save()
    {
        string fileName = StoreFileName;
        Directory.CreateDirectory(Path.GetDirectoryName(fileName)!);
        string tmp = fileName + ".tmp";
        File.WriteAllText(tmp, JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true }));
        File.Move(tmp, fileName, true);
    }

    public MonoPod.Core.ClockConfiguration Clock(DateTimeOffset now, long? preserveEpoch = null)
    {
        var zone = ManualTimeZone ? TimeZoneInfo.FindSystemTimeZoneById(ManualTimeZoneId) : TimeZoneInfo.Local;
        return new(preserveEpoch ?? now.ToUnixTimeSeconds(), MonoPod.Core.PosixTimeZone.FromWindows(zone, now), AutoNtp, NtpServer.Trim());
    }
}

public sealed class AppLog
{
    readonly List<string> lines = [];
    public event Action<string>? Added;
    public void Write(string message)
    {
        string text = $"{DateTime.Now:HH:mm:ss}  {message}";
        lines.Add(text);
        if (lines.Count > 10000)
            lines.RemoveRange(0, 1000);
        Added?.Invoke(text);
    }

    public void Save(string path) => File.WriteAllLines(path, lines);
}
