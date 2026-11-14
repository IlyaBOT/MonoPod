using System.Text.Json;

namespace MonoPod.Core;
public static class WireJson
{
    public static readonly JsonSerializerOptions Options = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,
        PropertyNameCaseInsensitive = true
    };
    public static T Read<T>(string json)
    {
        try
        {
            return JsonSerializer.Deserialize<T>(json, Options) ?? throw new InvalidDataException("Empty device response");
        }
        catch (JsonException e)
        {
            throw new InvalidDataException("Invalid MonoPod JSON response.", e);
        }
    }
}

public sealed record DeviceInfo
{
    public string DeviceGuid { get; init; } = "";
    public string Firmware { get; init; } = "Unknown";
    public int Protocol { get; init; }
    public bool SdReady { get; init; }
    public long TotalBytes { get; init; }
    public long FreeBytes { get; init; }
    public int Tracks { get; init; }
    public string SyncMode { get; init; } = "unknown";
    public string[] Capabilities { get; init; } = [];
    public string[] AudioFormats { get; init; } = [".mp3", ".wav", ".flac", ".aac"];
    public string Ip { get; init; } = "";
    public long ClockUnix { get; init; }
    public string Timezone { get; init; } = "";
    public bool NtpEnabled { get; init; }
    public string NtpServer { get; init; } = "pool.ntp.org";

    public bool Supports(string capability) => Capabilities.Contains(capability, StringComparer.OrdinalIgnoreCase);
}

public sealed record RemoteFileEntry
{
    public string Name { get; init; } = "";
    public string Path { get; init; } = "/";
    public bool Directory { get; init; }
    public long Size { get; init; }
    public long Modified { get; init; }
}

public sealed record DirectoryPage
{
    public RemoteFileEntry[] Entries { get; init; } = [];
    public int Next { get; init; } = -1;
}

public sealed record TransferProgress(string FileName, long Completed, long Total, TimeSpan Elapsed)
{
    public double BytesPerSecond => Elapsed.TotalSeconds > 0 ? Completed / Elapsed.TotalSeconds : 0;
    public double Percent => Total == 0 ? 100 : 100d * Completed / Total;
    public TimeSpan? Remaining => BytesPerSecond > 0 ? TimeSpan.FromSeconds((Total - Completed) / BytesPerSecond) : null;
}

public sealed class DeviceException(string message) : IOException(Friendly(message))
{
    // Preserve the wire code for recovery decisions; Message remains user-readable.
    public string Code { get; } = message.Split(' ', 2, StringSplitOptions.RemoveEmptyEntries).FirstOrDefault() ?? "";
    static string Friendly(string message)
    {
        (string Code, string Explanation)[] errors = [("NO_SD", "No mounted microSD card. Check the card on the player."), ("SYNC_REQUIRED", "Start Sync Mode before accessing microSD."), ("SYNC_BUSY", "Another transport owns microSD. Finish its Sync session first."), ("TRANSFER_ACTIVE", "A file operation is still active. Wait for it to finish."), ("ALREADY_EXISTS", "The destination already exists. Choose Replace or Skip."), ("NOT_FOUND", "The requested file or directory was not found."), ("NO_SPACE", "There is not enough free space to keep the old file and upload the new file."), ("DIRECTORY_NOT_EMPTY", "The folder is not empty. Delete its contents explicitly first."), ("PROTECTED_PATH", "This path belongs to a protected system area."), ("PROTECTED_ROOT", "This root folder cannot be deleted or renamed."), ("INVALID_PATH", "The microSD path is invalid or exceeds the filename limits."), ("INVALID_THEME_OR_CRC", "The device rejected the theme format, size or CRC32."), ("CRC_MISMATCH", "The transfer failed CRC32 verification. Retry the file."), ("RECOVERY_PENDING", "An earlier replacement needs recovery. Keep the backup and reconnect the player."), ("THEME_ACTIVATION_FAILED", "The device could not activate the theme; check its available memory."), ("SD_WRITE", "The device could not write to microSD."), ("SD_READ", "The device could not read microSD.")];
        foreach (var(code, explanation)in errors)
            if (message.Contains(code, StringComparison.Ordinal))
                return explanation + " (" + message + ")";
        return message;
    }
}

public interface IMonoPodTransport : IAsyncDisposable
{
    string Description { get; }

    bool IsUsb { get; }

    Task<DeviceInfo> ConnectAsync(CancellationToken ct);
    Task<DeviceInfo> GetDeviceInfoAsync(CancellationToken ct);
    Task StartSyncAsync(CancellationToken ct);
    Task EndSyncAsync(CancellationToken ct);
    Task<IReadOnlyList<RemoteFileEntry>> ListAsync(string path, CancellationToken ct);
    Task<RemoteFileEntry> StatAsync(string path, CancellationToken ct);
    Task<byte[]> ReadRangeAsync(string path, long offset, int count, CancellationToken ct);
    Task UploadAsync(string localPath, string remotePath, bool replace, IProgress<TransferProgress>? progress, CancellationToken ct);
    Task DownloadAsync(string remotePath, string localPath, IProgress<TransferProgress>? progress, CancellationToken ct);
    Task DeleteAsync(string path, bool directory, CancellationToken ct);
    Task CreateDirectoryAsync(string path, CancellationToken ct);
    Task RenameAsync(string path, string destination, CancellationToken ct);
    Task UploadThemeAsync(byte[] data, CancellationToken ct);
    Task ScanAsync(CancellationToken ct);
    Task SetSmoothingAsync(bool enabled, CancellationToken ct);
}

public static class RemotePath
{
    public static string Normalize(string path)
    {
        if (string.IsNullOrEmpty(path) || path[0] != '/' || path.Contains('\\'))
            throw new ArgumentException("An absolute microSD path is required.");
        var parts = path.Split('/', StringSplitOptions.RemoveEmptyEntries);
        if (parts.Any(p => p is "." or ".." || p.StartsWith('.') || p.EndsWith('.') || p.EndsWith(' ') || p.Any(c => c < 32 || c == 127 || ":<>\"|*?".Contains(c)) || System.Text.Encoding.UTF8.GetByteCount(p) > 90))
            throw new ArgumentException("Invalid or reserved microSD path.");
        var result = "/" + string.Join('/', parts);
        if (System.Text.Encoding.UTF8.GetByteCount(result) > 240)
            throw new ArgumentException("Path exceeds 240 UTF-8 bytes.");
        return result;
    }

    public static string Join(string parent, string name) => Normalize(parent.TrimEnd('/') + "/" + name);
    public static string Parent(string path)
    {
        path = Normalize(path);
        int i = path.LastIndexOf('/');
        return i <= 0 ? "/" : path[..i];
    }

    public static string Hex(string path) => Convert.ToHexString(System.Text.Encoding.UTF8.GetBytes(Normalize(path)));
    public static IEnumerable<RemoteFileEntry> Sort(IEnumerable<RemoteFileEntry> files, string column = "Name", bool descending = false)
    {
        Func<RemoteFileEntry, object> key = column switch
        {
            "Size" => x => x.Size,
            "Modified" => x => x.Modified,
            _ => x => x.Name.ToUpperInvariant()};
        var groups = files.OrderBy(x => !x.Directory);
        return descending ? groups.ThenByDescending(key).ThenBy(x => x.Name, StringComparer.OrdinalIgnoreCase) : groups.ThenBy(key).ThenBy(x => x.Name, StringComparer.OrdinalIgnoreCase);
    }
}

public static class DisplayValue
{
    public static string Size(long bytes) => bytes < 1024 ? $"{bytes} B" : bytes < 1048576 ? $"{bytes / 1024d:F1} KiB" : bytes < 1073741824 ? $"{bytes / 1048576d:F1} MiB" : $"{bytes / 1073741824d:F2} GiB";
    public static string Duration(double? seconds) => seconds is null ? "—" : seconds >= 3600 ? $"{(long)seconds.Value / 3600}:{(long)seconds.Value / 60 % 60:00}:{(long)seconds.Value % 60:00}" : TimeSpan.FromSeconds(seconds.Value).ToString(@"mm\:ss");
}
