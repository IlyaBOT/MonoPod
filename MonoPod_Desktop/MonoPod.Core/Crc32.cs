namespace MonoPod.Core;
public sealed class Crc32
{
    private uint value = 0xffffffff;
    static readonly uint[] Table = BuildTable();
    static uint[] BuildTable()
    {
        var result = new uint[256];
        for (uint n = 0; n < 256; n++)
        {
            uint c = n;
            for (int i = 0; i < 8; i++)
                c = (c >> 1) ^ ((c & 1) != 0 ? 0xedb88320u : 0);
            result[n] = c;
        }

        return result;
    }

    public void Append(ReadOnlySpan<byte> bytes)
    {
        foreach (byte b in bytes)
            value = Table[(value ^ b) & 255] ^ (value >> 8);
    }

    public uint Value => ~value;

    public static uint Compute(ReadOnlySpan<byte> bytes)
    {
        var c = new Crc32();
        c.Append(bytes);
        return c.Value;
    }

    public static Task<uint> FileAsync(string path, CancellationToken ct) => Task.Run(async () =>
    {
        await using var stream = File.OpenRead(path);
        byte[] buffer = new byte[65536];
        var c = new Crc32();
        int n;
        while ((n = await stream.ReadAsync(buffer, ct)) != 0)
            c.Append(buffer.AsSpan(0, n));
        return c.Value;
    }, ct);
}

public static class Downloads
{
    public static string LocalTarget(string folder, string name)
    {
        if (string.IsNullOrWhiteSpace(name) || name != Path.GetFileName(name) || name.IndexOfAny(['/', '\\', ':']) >= 0)
            throw new InvalidDataException("Device returned an invalid filename.");
        RemotePath.Join("/", name);
        string root = Path.TrimEndingDirectorySeparator(Path.GetFullPath(folder));
        string result = Path.GetFullPath(Path.Combine(root, name));
        if (!string.Equals(Path.TrimEndingDirectorySeparator(Path.GetDirectoryName(result)!), root, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("Download target is outside the selected folder.");
        return result;
    }

    public static async Task SaveAsync(IMonoPodTransport transport, string remote, string local, IProgress<TransferProgress>? progress, CancellationToken ct)
    {
        var stat = await transport.StatAsync(remote, ct);
        if (stat.Directory)
            throw new IOException("Select a file to download.");
        string part = local + ".monopod-" + Guid.NewGuid().ToString("N") + ".part";
        var watch = System.Diagnostics.Stopwatch.StartNew();
        try
        {
            await using (var output = new FileStream(part, FileMode.CreateNew, FileAccess.Write, FileShare.None, 65536, true))
            {
                long offset = 0;
                while (offset < stat.Size)
                {
                    ct.ThrowIfCancellationRequested();
                    var data = await transport.ReadRangeAsync(remote, offset, (int)Math.Min(16384, stat.Size - offset), ct);
                    if (data.Length == 0)
                        throw new EndOfStreamException();
                    await output.WriteAsync(data, ct);
                    offset += data.Length;
                    progress?.Report(new(stat.Name, offset, stat.Size, watch.Elapsed));
                }

                await output.FlushAsync(ct);
                output.Flush(true);
            }

            var after = await transport.StatAsync(remote, ct);
            if (after.Size != stat.Size || after.Modified != stat.Modified)
                throw new IOException("Remote file changed during download.");
            ct.ThrowIfCancellationRequested();
            File.Move(part, local, true);
        }
        finally
        {
            if (File.Exists(part))
                File.Delete(part);
        }
    }
}
