using MonoPod.Core;
using System.Text;

namespace MonoPod.Desktop.Tests;
// Stateful device double: destination changes only after exact length and CRC verification.
public sealed class UsbDevice : ISerialLink
{
    public byte[]? Database { get; set; }
    public long FreeBytes { get; set; }=30000000000;
    readonly Queue<byte> replies = [];
    readonly List<byte> command = [];
    readonly MemoryStream part = new();
    long wanted;
    uint checksum;
    string destination = "";
    bool binary, replace;
    public Dictionary<string, byte[]> Files { get; } = new()
    {
        ["/Music/existing.mp3"] = [1, 2, 3]
    };
    public List<string> Commands { get; } = [];
    public bool Sync { get; set; }
    public bool Disposed { get; private set; }
    public int? DisconnectAt { get; set; }
    public bool BadNext { get; set; }
    public bool BadReadCrc { get; set; }
    public bool BadFinalCrc { get; set; }
    public string? HelloOverride { get; set; }
    public string? StartReplyOverride { get; set; }
    public string? FinalReplyOverride { get; set; }
    public int StartupNoiseLines { get; set; }
    public int? SdWriteErrorAt { get; set; }
    public bool Smoothing { get; private set; }
    public bool ClockSupported { get; set; }
    public bool CustomNtp { get; set; }
    public ClockConfiguration? Clock { get; private set; }

    void Reply(string s)
    {
        foreach (byte b in Encoding.UTF8.GetBytes("MPOD1 " + s + "\n"))
            replies.Enqueue(b);
    }

    static string PathOf(string hex) => Encoding.UTF8.GetString(Convert.FromHexString(hex));
    public int Read(byte[] b, int off, int n)
    {
        if (Disposed)
            throw new IOException("Disconnected");
        if (replies.Count == 0)
            throw new TimeoutException();
        int count = Math.Min(n, Math.Min(37, replies.Count));
        for (int i = 0; i < count; i++)
            b[off + i] = replies.Dequeue();
        return count;
    }

    public void Write(byte[] b, int off, int n)
    {
        if (Disposed)
            throw new IOException("Disconnected");
        if (binary)
        {
            if (DisconnectAt is { } at && part.Length + n >= at)
            {
                Disposed = true;
                throw new IOException("Cable removed");
            }

            part.Write(b, off, n);
            if (SdWriteErrorAt is { } failedAt && part.Length >= failedAt)
            {
                // UsbSync::fail aborts the transaction and enters its drain period.
                // A client must close this uncertain stream, not append commands.
                binary = false;
                part.SetLength(0);
                Reply("ERR SD_WRITE");
                return;
            }
            if (part.Length == wanted)
            {
                binary = false;
                if ((Crc32.Compute(part.ToArray()) != checksum) || BadFinalCrc)
                    Reply("ERR SIZE_OR_CRC_MISMATCH");
                else
                {
                    Files[destination] = part.ToArray();
                    Reply(FinalReplyOverride ?? "OK SAVED");
                }
            }
            else
                Reply("NEXT " + Math.Min(1024, wanted - part.Length));
            return;
        }

        for (int i = off; i < off + n; i++)
        {
            if (b[i] == 10)
            {
                string text = Encoding.UTF8.GetString(command.ToArray());
                command.Clear();
                Handle(text);
            }
            else
                command.Add(b[i]);
        }
    }

    void Handle(string text)
    {
        Commands.Add(text);
        string s = text[6..];
        if (s == "HELLO")
        {
            for (int i = 0; i < StartupNoiseLines; i++)
                foreach (byte b in Encoding.UTF8.GetBytes($"Startup diagnostic {i}\r\n"))
                    replies.Enqueue(b);
            Reply(HelloOverride ?? "READY 4 SD=1");
            return;
        }

        if (s == "SYNC START")
        {
            Sync = true;
            Reply(StartReplyOverride ?? "OK SYNC_STARTED");
            return;
        }

        if (s == "SYNC END")
        {
            if (!Sync)
            {
                Reply("ERR NO_USB_SYNC");
                return;
            }
            Sync = false;
            Reply("OK SYNC_ENDED");
            return;
        }

        if (s == "X INFO")
        {
            var caps = new List<string> { "list", "read", "stat", "theme", "smoothing" };
            if(Database!=null)caps.Add("library_db_v1");
            if (ClockSupported) caps.Add("clock_sync");
            if (CustomNtp) caps.Add("ntp_server");
            Reply("OK " + System.Text.Json.JsonSerializer.Serialize(new DeviceInfo
            {
                Firmware = "0.10.6", Protocol = 4, SdReady = true, TotalBytes = 34359738368, FreeBytes = FreeBytes,
                SyncMode = Sync ? "usb" : "none", Capabilities = caps.ToArray(), ClockUnix = Clock?.UnixSeconds ?? 0,
                Timezone = Clock?.Timezone ?? "", NtpEnabled = Clock?.NtpEnabled ?? false, NtpServer = Clock?.NtpServer ?? "pool.ntp.org"
            }, WireJson.Options));
            return;
        }

        if (!Sync)
        {
            Reply("ERR SYNC_REQUIRED");
            return;
        }

        if (s == "SCAN")
        {
            Reply("OK SCANNED 1");
            return;
        }

        var t = s.Split(' ');
        if (t[0] != "X")
        {
            Reply("ERR UNKNOWN_COMMAND");
            return;
        }

        string op = t[1];
        if(op=="DBINFO" && Database!=null){Reply($"OK {{\"schema\":1,\"generation\":7,\"count\":54,\"size\":{Database.Length},\"dirty\":false}}");return;}
        if(op=="DBREFRESH" && Database!=null){Reply("OK GENERATION 7");return;}
        if(op=="DBREAD" && Database!=null)
        {int offset=int.Parse(t[2]),count=int.Parse(t[3]);var data=Database.AsSpan(offset,count).ToArray();Reply($"DATA {count} {Crc32.Compute(data):X8}");foreach(byte v in data)replies.Enqueue(v);return;}
        if (op == "CLOCK")
        {
            Clock = new(long.Parse(t[2]), PathOf(t[3]), t[4] == "1", t.Length > 5 ? PathOf(t[5]) : "pool.ntp.org");
            Reply("OK CLOCK_SET"); return;
        }
        if (op == "SMOOTH")
        {
            if (t.Length != 3 || t[2] is not ("0" or "1"))
                Reply("ERR ARGUMENTS");
            else
            {
                Smoothing = t[2] == "1";
                Reply("OK SMOOTHING");
            }
            return;
        }

        string path = t.Length > 2 ? PathOf(t[2]) : "";
        if (op is "PUT" or "THEME")
        {
            destination = path;
            wanted = long.Parse(t[3]);
            checksum = Convert.ToUInt32(t[4], 16);
            replace = t[5] == "1";
            if (Files.ContainsKey(path) && !replace)
            {
                Reply("ERR ALREADY_EXISTS");
                return;
            }

            part.SetLength(0);
            binary = wanted > 0;
            if (!binary)
            {
                Files[destination] = [];
                Reply(FinalReplyOverride ?? "OK SAVED");
                return;
            }

            Reply(BadNext ? "NEXT 999999" : "NEXT " + Math.Min(1024, wanted));
            return;
        }

        if (op == "STAT")
        {
            if (!Files.TryGetValue(path, out var data))
            {
                Reply("ERR NOT_FOUND");
                return;
            }

            Reply("OK " + System.Text.Json.JsonSerializer.Serialize(new RemoteFileEntry { Name = System.IO.Path.GetFileName(path), Path = path, Size = data.Length }, WireJson.Options));
            return;
        }

        if (op == "READ")
        {
            if (!Files.TryGetValue(path, out var data))
            {
                Reply("ERR NOT_FOUND");
                return;
            }

            int offset = int.Parse(t[3]), count = int.Parse(t[4]);
            var bytes = data.Skip(offset).Take(count).ToArray();
            uint crc = Crc32.Compute(bytes);
            Reply($"DATA {bytes.Length} {(BadReadCrc ? crc + 1 : crc):X8}");
            foreach (byte b in bytes)
                replies.Enqueue(b);
            return;
        }

        if (op == "LIST")
        {
            int offset = int.Parse(t[3]), limit = int.Parse(t[4]);
            var all = Files.Select(p => new RemoteFileEntry { Path = p.Key, Name = System.IO.Path.GetFileName(p.Key), Size = p.Value.Length }).ToArray();
            Reply("OK " + System.Text.Json.JsonSerializer.Serialize(new DirectoryPage { Entries = all.Skip(offset).Take(limit).ToArray(), Next = offset + limit < all.Length ? offset + limit : -1 }, WireJson.Options));
            return;
        }

        if (op is "DELETE" or "RMDIR")
        {
            Files.Remove(path);
            Reply("OK DONE");
            return;
        }

        if (op == "RENAME")
        {
            string dest = PathOf(t[3]);
            Files[dest] = Files[path];
            Files.Remove(path);
            Reply("OK DONE");
            return;
        }

        if (op == "MKDIR")
        {
            Reply("OK DONE");
            return;
        }

        Reply("ERR UNKNOWN_COMMAND");
    }

    public void Dispose() => Disposed = true;
}
