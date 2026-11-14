using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using MonoPod.Core;

namespace MonoPod.Desktop.Tests;
public sealed class HttpDevice : IAsyncDisposable
{
    readonly HttpListener server = new();
    readonly CancellationTokenSource cancel = new();
    readonly Task loop;
    public string Url { get; }
    public Dictionary<string, byte[]> Files { get; } = new()
    {
        ["/Music/Песня.mp3"] = [1, 2, 3, 4]
    };
    public HashSet<string> Folders { get; } = ["/", "/Music"];
    public bool Sync { get; set; } = true;
    public bool SdReady { get; set; } = true;
    public int? FailCode { get; set; }
    public bool BadCrc { get; set; }
    public bool DropRead { get; set; }
    public int PageCount { get; private set; }
    public byte[]? Theme { get; private set; }
    public List<(string Method, string Path, string Query)> Requests { get; } = [];

    public HttpDevice()
    {
        var socket = new TcpListener(IPAddress.Loopback, 0);
        socket.Start();
        int port = ((IPEndPoint)socket.LocalEndpoint).Port;
        socket.Stop();
        Url = $"http://127.0.0.1:{port}/";
        server.Prefixes.Add(Url);
        server.Start();
        loop = Loop();
    }

    async Task Loop()
    {
        try
        {
            while (!cancel.IsCancellationRequested)
            {
                var context = await server.GetContextAsync().WaitAsync(cancel.Token);
                try
                {
                    await Handle(context);
                }
                catch (Exception)
                {
                    context.Response.Abort();
                }
            }
        }
        catch (OperationCanceledException)
        {
        }
        catch (HttpListenerException)
        {
        }
        catch (ObjectDisposedException)
        {
        }
    }

    static byte[] Json(object value) => Encoding.UTF8.GetBytes(JsonSerializer.Serialize(value, WireJson.Options));
    async Task Send(HttpListenerContext c, int status, byte[] data, string type = "application/json")
    {
        c.Response.StatusCode = status;
        c.Response.ContentType = type;
        c.Response.ContentLength64 = data.Length;
        await c.Response.OutputStream.WriteAsync(data);
        c.Response.Close();
    }

    Task Error(HttpListenerContext c, int status, string msg) => Send(c, status, Encoding.UTF8.GetBytes(msg), "text/plain");
    async Task Handle(HttpListenerContext c)
    {
        var q = c.Request.QueryString;
        string route = c.Request.Url!.AbsolutePath, path = q["path"] ?? "/";
        Requests.Add((c.Request.HttpMethod, route, c.Request.Url.Query));
        if (FailCode is { } code)
        {
            await Error(c, code, "Injected device error");
            return;
        }

        if (route == "/api/info")
        {
            await Send(c, 200, Json(new DeviceInfo { Firmware = "0.10.6", Protocol = 4, SdReady = SdReady, TotalBytes = 64000000000, FreeBytes = 32000000000, SyncMode = Sync ? "wifi" : "none", Capabilities = ["list", "stat", "read", "fs_upload", "delete", "mkdir", "rename", "theme", "sync_end", "scan"] }));
            return;
        }

        if (!SdReady || !Sync)
        {
            await Error(c, 409, !SdReady ? "NO_SD" : "SYNC_REQUIRED");
            return;
        }

        RemoteFileEntry Entry(string p, byte[]? data) => new()
        {
            Path = p,
            Name = Path.GetFileName(p),
            Directory = data == null,
            Size = data?.Length ?? 0
        };
        if (route == "/api/fs/list")
        {
            PageCount++;
            if (!Folders.Contains(path))
            {
                await Error(c, 404, "NOT_FOUND");
                return;
            }

            int offset = int.Parse(q["offset"] ?? "0"), limit = int.Parse(q["limit"] ?? "32");
            var entries = Files.Where(x => RemotePath.Parent(x.Key) == path).Select(x => Entry(x.Key, x.Value)).Concat(Folders.Where(x => x != "/" && RemotePath.Parent(x) == path).Select(x => Entry(x, null))).ToArray();
            await Send(c, 200, Json(new DirectoryPage { Entries = entries.Skip(offset).Take(limit).ToArray(), Next = offset + limit < entries.Length ? offset + limit : -1 }));
            return;
        }

        if (route == "/api/fs/stat")
        {
            if (Files.TryGetValue(path, out var bytes))
                await Send(c, 200, Json(Entry(path, bytes)));
            else if (Folders.Contains(path))
                await Send(c, 200, Json(Entry(path, null)));
            else
                await Error(c, 404, "NOT_FOUND");
            return;
        }

        if (route == "/api/fs/download")
        {
            if (DropRead)
            {
                c.Response.Abort();
                return;
            }

            if (!Files.TryGetValue(path, out var bytes))
            {
                await Error(c, 404, "NOT_FOUND");
                return;
            }

            int offset = int.Parse(q["offset"]!), count = int.Parse(q["count"]!);
            byte[] data = bytes.Skip(offset).Take(count).ToArray();
            c.Response.Headers.Add("X-CRC32", (Crc32.Compute(data) + (BadCrc ? 1u : 0u)).ToString("X8"));
            await Send(c, 200, data, "application/octet-stream");
            return;
        }

        if (route is "/api/fs/upload" or "/api/theme")
        {
            // Match WiFiSync.cpp: only the dedicated URI enters the theme path.
            // A theme=1 query on the generic endpoint has no special meaning.
            bool theme = route == "/api/theme";
            path = theme ? "/MonoPod/theme.mpt" : path;
            if (c.Request.HttpMethod != "POST")
            {
                await Error(c, 404, "Not found");
                return;
            }
            if (!uint.TryParse(q["size"], out uint size) || size > 1073741824 ||
                q["crc"]?.Length != 8 || !uint.TryParse(q["crc"], System.Globalization.NumberStyles.HexNumber, null, out uint crc))
            {
                await Error(c, 422, "INVALID_ARGUMENTS");
                return;
            }
            if (!theme && path.StartsWith("/MonoPod/", StringComparison.OrdinalIgnoreCase) &&
                !path.Equals("/MonoPod/badapple.bav", StringComparison.OrdinalIgnoreCase))
            {
                await Error(c, 422, "PROTECTED_PATH");
                return;
            }
            using var buffer = new MemoryStream();
            await c.Request.InputStream.CopyToAsync(buffer);
            byte[] raw = buffer.ToArray();
            int start = raw.AsSpan().IndexOf("\r\n\r\n"u8) + 4;
            string boundary = c.Request.ContentType!.Split("boundary=", 2)[1].Trim('"');
            byte[] closing = Encoding.ASCII.GetBytes("\r\n--" + boundary + "--");
            int end = raw.AsSpan().LastIndexOf(closing);
            if (start < 4 || end < start)
            {
                await Error(c, 400, "MULTIPART");
                return;
            }

            byte[] data = raw.AsSpan(start, end - start).ToArray();
            if (data.Length != size || Crc32.Compute(data) != crc)
            {
                await Error(c, 422, "SIZE_OR_CRC_MISMATCH");
                return;
            }

            if (Files.ContainsKey(path) && q["replace"] != "1")
            {
                await Error(c, 422, "ALREADY_EXISTS");
                return;
            }

            if (theme)
            {
                try { ThemeDocument.Decode(data); }
                catch (InvalidDataException)
                {
                    await Error(c, 422, "INVALID_THEME_OR_CRC");
                    return;
                }
            }

            Files[path] = data;
            if (theme)
                Theme = data;
            await Send(c, 200, Json(new { ok = true }));
            return;
        }

        if (route == "/api/fs/mkdir")
        {
            Folders.Add(path);
            await Send(c, 200, Json(new { ok = true }));
            return;
        }

        if (route == "/api/fs/rename")
        {
            Files[q["destination"]!] = Files[path];
            Files.Remove(path);
            await Send(c, 200, Json(new { ok = true }));
            return;
        }

        if (route == "/api/fs/file")
        {
            Files.Remove(path);
            Folders.Remove(path);
            await Send(c, 200, Json(new { ok = true }));
            return;
        }

        if (route == "/api/scan")
        {
            await Send(c, 200, Json(new { ok = true }));
            return;
        }

        if (route == "/api/sync/end")
        {
            Sync = false;
            await Send(c, 200, Json(new { ok = true }));
            return;
        }

        await Error(c, 404, "NOT_FOUND");
    }

    public async ValueTask DisposeAsync()
    {
        cancel.Cancel();
        server.Close();
        await loop;
        cancel.Dispose();
    }
}
