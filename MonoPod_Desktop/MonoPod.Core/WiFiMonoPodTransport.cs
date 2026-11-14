using System.Net;
using System.Net.Http.Headers;
using System.Diagnostics;
using System.Text.Json;

namespace MonoPod.Core;
public sealed partial class WiFiMonoPodTransport : IMonoPodTransport, IClockTransport
{
    readonly HttpClient client;
    readonly TimeSpan requestTimeout, transferIdleTimeout;
    DeviceInfo info = new();
    public bool IsUsb => false;
    public string Description => "Wi-Fi | " + client.BaseAddress!.Authority;

    public WiFiMonoPodTransport(string address, HttpMessageHandler? handler = null, TimeSpan? requestTimeout = null, TimeSpan? transferIdleTimeout = null)
    {
        if (!address.Contains("://"))
            address = "http://" + address;
        if (!Uri.TryCreate(address, UriKind.Absolute, out var uri) || uri.Scheme != "http" || !string.IsNullOrEmpty(uri.UserInfo) || uri.AbsolutePath != "/" || !string.IsNullOrEmpty(uri.Query) || !string.IsNullOrEmpty(uri.Fragment))
            throw new ArgumentException("Enter an HTTP device address, for example http://192.168.1.123");
        client = handler == null ? new HttpClient(new SocketsHttpHandler { ConnectTimeout = TimeSpan.FromSeconds(8), AllowAutoRedirect = false, UseProxy = false }) : new HttpClient(handler);
        client.BaseAddress = uri;
        client.Timeout = Timeout.InfiniteTimeSpan;
        this.requestTimeout = requestTimeout ?? TimeSpan.FromSeconds(20);
        this.transferIdleTimeout = transferIdleTimeout ?? TimeSpan.FromSeconds(60);
    }

    static string Q(string path) => Uri.EscapeDataString(RemotePath.Normalize(path));
    async Task<string> Response(HttpResponseMessage response, CancellationToken ct, bool requireAcknowledgement = false)
    {
        using (response)
        {
            string body = await response.Content.ReadAsStringAsync(ct);
            if (!response.IsSuccessStatusCode)
                throw new DeviceException($"HTTP {(int)response.StatusCode}: {body}");
            if (requireAcknowledgement)
            {
                try
                {
                    using var json = JsonDocument.Parse(body);
                    if (json.RootElement.ValueKind != JsonValueKind.Object ||
                        !json.RootElement.TryGetProperty("ok", out var ok) || ok.ValueKind != JsonValueKind.True)
                        throw new InvalidDataException("MonoPod did not acknowledge the operation (expected {\"ok\":true}).");
                }
                catch (JsonException e)
                {
                    throw new InvalidDataException("Invalid MonoPod operation acknowledgement.", e);
                }
            }
            return body;
        }
    }

    CancellationTokenSource Deadline(CancellationToken ct, TimeSpan? budget = null)
    {
        var linked = CancellationTokenSource.CreateLinkedTokenSource(ct);
        linked.CancelAfter(budget ?? requestTimeout);
        return linked;
    }

    async Task<string> Get(string path, CancellationToken ct)
    {
        using var ownership = EnterNormalRequest();
        using var deadline = Deadline(ct);
        return await Response(await client.GetAsync(path, deadline.Token), deadline.Token);
    }

    async Task Post(string path, CancellationToken ct)
    {
        using var ownership = EnterNormalRequest();
        using var deadline = Deadline(ct, path is "api/scan" or "api/sync/end" or "api/library/refresh" ? TimeSpan.FromMinutes(5) : null);
        await Response(await client.PostAsync(path, new ByteArrayContent([]), deadline.Token), deadline.Token, info.Protocol >= 4);
    }

    async Task UploadForm(string path, Stream input, string name, string field, IProgress<TransferProgress>? progress, CancellationToken ct)
    {
        using var ownership = EnterNormalRequest();
        using var deadline = Deadline(ct, transferIdleTimeout);
        using var content = new MultipartFormDataContent();
        content.Add(new ProgressStreamContent(input, progress, name, deadline.Token, () => deadline.CancelAfter(transferIdleTimeout)), field, name);
        await Response(await client.PostAsync(path, content, deadline.Token), deadline.Token, info.Protocol >= 4);
    }

    public async Task<DeviceInfo> ConnectAsync(CancellationToken ct)
    {
        try
        {
            return await GetDeviceInfoAsync(ct);
        }
        catch (HttpRequestException e)
        {
            throw new IOException("MonoPod is unavailable. Open Wi-Fi Sync on the player and check its displayed IP address.", e);
        }
    }

    public async Task<DeviceInfo> GetDeviceInfoAsync(CancellationToken ct)
    {
        string body = await Get("api/info", ct);
        info = WireJson.Read<DeviceInfo>(body);
        if (info.Protocol == 0)
        {
            using var doc = System.Text.Json.JsonDocument.Parse(body);
            long N(string k) => doc.RootElement.TryGetProperty(k, out var v) ? v.GetInt64() : 0;
            info = info with
            {
                TotalBytes = N("sd_total_mb") * 1048576,
                FreeBytes = (N("sd_total_mb") - N("sd_used_mb")) * 1048576,
                SyncMode = "wifi",
                Capabilities = ["upload", "theme"]
            };
        }

        return info;
    }

    void Need(string capability)
    {
        if (!info.Supports(capability))
            throw new NotSupportedException("Unsupported: this firmware does not advertise " + capability + ". MonoPod 0.10.6 / MPOD1 v4 is supported.");
    }

    public async Task StartSyncAsync(CancellationToken ct)
    {
        // Wi-Fi Sync is entered on the player, not by a start HTTP endpoint.
        // Refresh instead of trusting a session that may have ended on the player.
        var current = await GetDeviceInfoAsync(ct);
        if (!current.SdReady)
            throw new DeviceException("NO_SD");
        if (current.SyncMode != "wifi")
            throw new DeviceException("Wi-Fi Sync is not active. Open Wi-Fi Sync on the player.");
    }

    public async Task EndSyncAsync(CancellationToken ct)
    {
        if (!info.Supports("sync_end"))
            return;
        await Post("api/sync/end", ct);
        info = info with { SyncMode = "none" };
    }
    public async Task<IReadOnlyList<RemoteFileEntry>> ListAsync(string path, CancellationToken ct)
    {
        Need("list");
        var result = new List<RemoteFileEntry>();
        int offset = 0;
        while (true)
        {
            var page = WireJson.Read<DirectoryPage>(await Get($"api/fs/list?path={Q(path)}&offset={offset}&limit=32", ct));
            result.AddRange(page.Entries);
            if (page.Next < 0)
                break;
            if (page.Next <= offset)
                throw new InvalidDataException("Invalid pagination cursor.");
            offset = page.Next;
        }

        return result;
    }

    public async Task<RemoteFileEntry> StatAsync(string path, CancellationToken ct)
    {
        Need("stat");
        return WireJson.Read<RemoteFileEntry>(await Get("api/fs/stat?path=" + Q(path), ct));
    }

    public async Task<byte[]> ReadRangeAsync(string path, long offset, int count, CancellationToken ct)
    {
        using var ownership = EnterNormalRequest();
        Need("read");
        if (offset < 0 || count < 0 || count > 65536)
            throw new ArgumentOutOfRangeException(nameof(count));
        using var deadline = Deadline(ct);
        ct = deadline.Token;
        using var result = new MemoryStream();
        while (result.Length < count)
        {
            int request = Math.Min(16384, count - (int)result.Length);
            using var response = await client.GetAsync($"api/fs/download?path={Q(path)}&offset={offset + result.Length}&count={request}", HttpCompletionOption.ResponseHeadersRead, ct);
            if (!response.IsSuccessStatusCode)
                throw new DeviceException($"HTTP {(int)response.StatusCode}: " + await response.Content.ReadAsStringAsync(ct));
            var data = await response.Content.ReadAsByteArrayAsync(ct);
            if (data.Length > request || !response.Headers.TryGetValues("X-CRC32", out var values) || !uint.TryParse(values.Single(), System.Globalization.NumberStyles.HexNumber, null, out uint crc) || Crc32.Compute(data) != crc)
                throw new InvalidDataException("Download size / CRC32 mismatch.");
            result.Write(data);
            if (data.Length < request)
                break;
        }

        return result.ToArray();
    }

    public async Task UploadAsync(string local, string remote, bool replace, IProgress<TransferProgress>? progress, CancellationToken ct)
    {
        using var ownership = EnterNormalRequest();
        if (!info.Supports("fs_upload"))
        {
            if (RemotePath.Parent(remote) != "/Music")
                throw new NotSupportedException("Legacy Wi-Fi can upload music to /Music only.");
            await using var legacyStream = File.OpenRead(local);
            await UploadForm("upload", legacyStream, Path.GetFileName(local), "track", progress, ct);
            return;
        }

        uint crc = await Crc32.FileAsync(local, ct);
        await using var stream = File.OpenRead(local);
        await UploadForm($"api/fs/upload?path={Q(remote)}&size={stream.Length}&crc={crc:X8}&replace={(replace ? 1 : 0)}", stream, Path.GetFileName(local), "file", progress, ct);
    }

    public async Task UploadThemeAsync(byte[] data, CancellationToken ct)
    {
        Need("theme");
        // Firmware selects its protected theme transaction by URI, not theme=1.
        // The dedicated v4 endpoint still requires size/whole-file CRC/replace.
        var theme = ThemeDocument.Decode(data);
        if (info.Protocol < 4)
            data = theme.Encode(false);
        string path = info.Protocol >= 4 ? $"api/theme?size={data.Length}&crc={Crc32.Compute(data):X8}&replace=1" : "api/theme";
        using var input = new MemoryStream(data, false);
        await UploadForm(path, input, "theme.mpt", "theme", null, ct);
    }

    public Task DownloadAsync(string remote, string local, IProgress<TransferProgress>? progress, CancellationToken ct) => Downloads.SaveAsync(this, remote, local, progress, ct);
    public async Task DeleteAsync(string path, bool directory, CancellationToken ct)
    {
        using var ownership = EnterNormalRequest();
        Need("delete");
        using var deadline = Deadline(ct);
        ct = deadline.Token;
        await Response(await client.DeleteAsync($"api/fs/file?path={Q(path)}&directory={(directory ? 1 : 0)}", ct), ct, info.Protocol >= 4);
    }

    public Task CreateDirectoryAsync(string path, CancellationToken ct)
    {
        Need("mkdir");
        return Post("api/fs/mkdir?path=" + Q(path), ct);
    }

    public Task RenameAsync(string path, string destination, CancellationToken ct)
    {
        Need("rename");
        return Post($"api/fs/rename?path={Q(path)}&destination={Q(destination)}", ct);
    }

    public Task ScanAsync(CancellationToken ct)
    {
        Need("scan");
        return Post("api/scan", ct);
    }

    public Task SetSmoothingAsync(bool enabled, CancellationToken ct) => Post("api/smoothing?enabled=" + (enabled ? 1 : 0), ct);
    public async Task SetClockAsync(ClockConfiguration clock, CancellationToken ct)
    {
        Need("clock_sync");
        clock.Validate(info.Supports("ntp_server"));
        using var ownership = EnterNormalRequest();
        using var deadline = Deadline(ct);
        var fields = new Dictionary<string,string>
        {
            ["unix"] = clock.UnixSeconds.ToString(System.Globalization.CultureInfo.InvariantCulture),
            ["timezone"] = clock.Timezone, ["ntp"] = clock.NtpEnabled ? "1" : "0"
        };
        if (info.Supports("ntp_server")) fields["ntp_server"] = clock.NtpServer;
        using var body = new FormUrlEncodedContent(fields);
        await Response(await client.PostAsync("api/clock", body, deadline.Token), deadline.Token, requireAcknowledgement: true);
    }
    public ValueTask DisposeAsync()
    {
        client.Dispose();
        return ValueTask.CompletedTask;
    }

    sealed class ProgressStreamContent(Stream input, IProgress<TransferProgress>? progress, string name, CancellationToken ct, Action pulse) : HttpContent
    {
        protected override bool TryComputeLength(out long length)
        {
            length = input.Length;
            return true;
        }

        protected override async Task SerializeToStreamAsync(Stream target, TransportContext? context)
        {
            var clock = Stopwatch.StartNew();
            byte[] buffer = new byte[16384];
            long sent = 0;
            int n;
            while ((n = await input.ReadAsync(buffer, ct)) != 0)
            {
                await target.WriteAsync(buffer.AsMemory(0, n), ct);
                pulse();
                sent += n;
                progress?.Report(new(name, sent, input.Length, clock.Elapsed));
            }
        }
    }
}
