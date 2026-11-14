using System.Text;
using System.Diagnostics;

namespace MonoPod.Core;
public sealed partial class UsbMonoPodTransport : IMonoPodTransport, IClockTransport
{
    readonly Func<ISerialLink> factory;
    readonly TimeSpan responseTimeout;
    readonly TimeSpan transferResponseTimeout, commitResponseTimeout, recoveryQuietPeriod;
    TimeSpan activeResponseTimeout;
    ISerialLink? link;
    readonly SemaphoreSlim gate = new(1, 1);
    DeviceInfo info = new();
    bool sync, poisoned, disposed, recoveryPending;
    long lastWriteTimestamp;
    string phase = "waiting for a reply";
    public string Description { get; }
    public bool IsUsb => true;
    public bool NeedsRecovery => recoveryPending && !disposed;

    public UsbMonoPodTransport(string port) : this(() => new SerialLink(port), "USB | " + port)
    {
    }

    public UsbMonoPodTransport(Func<ISerialLink> factory, string description = "USB mock", TimeSpan? responseTimeout = null,
        TimeSpan? recoveryQuietPeriod = null)
    {
        this.factory = factory;
        Description = description;
        this.responseTimeout = responseTimeout ?? TimeSpan.FromSeconds(12);
        transferResponseTimeout = responseTimeout ?? TimeSpan.FromSeconds(20);
        commitResponseTimeout = responseTimeout ?? TimeSpan.FromSeconds(60);
        this.recoveryQuietPeriod = recoveryQuietPeriod ?? TimeSpan.FromSeconds(18);
        activeResponseTimeout = this.responseTimeout;
    }

    Task<T> Run<T>(Func<CancellationToken, T> fn, CancellationToken ct) => Task.Run(async () =>
    {
        await gate.WaitAsync(ct);
        try
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            if (poisoned)
                throw new IOException("USB stream interrupted. Reconnect after the device receive timeout (18 seconds).");
            return fn(ct);
        }
        catch (DeviceException e)
        {
            if (e.Code is "SYNC_REQUIRED" or "NO_USB_SYNC" or "SYNC_BUSY" or "NO_SD")
                sync = false;
            throw;
        }
        catch (Exception e)when (e is IOException or InvalidDataException or TimeoutException or OperationCanceledException)
        {
            Poison();
            throw;
        }
        finally
        {
            gate.Release();
        }
    }, ct);
    void Send(string s)
    {
        var b = Encoding.UTF8.GetBytes("MPOD1 " + s + "\n");
        Write(b, 0, b.Length);
    }

    void Write(byte[] bytes, int offset, int count)
    {
        try { link!.Write(bytes, offset, count); }
        finally { lastWriteTimestamp = Stopwatch.GetTimestamp(); }
    }

    TimeoutException Timeout() => new("MonoPod USB response timed out (" + phase + ").");

    byte[] Read(int count, CancellationToken ct, Stopwatch? deadline = null)
    {
        var bytes = new byte[count];
        int pos = 0;
        var watch = deadline ?? Stopwatch.StartNew();
        while (pos < count)
        {
            ct.ThrowIfCancellationRequested();
            if (watch.Elapsed > activeResponseTimeout)
                throw Timeout();
            try
            {
                int n = link!.Read(bytes, pos, count - pos);
                if (n == 0)
                    throw new EndOfStreamException("USB disconnected.");
                pos += n;
            }
            catch (TimeoutException)
            {
                if (watch.Elapsed > activeResponseTimeout)
                    throw Timeout();
            }
        }

        return bytes;
    }

    string Reply(CancellationToken ct, bool ignorePriorReceiveAbort = false)
    {
        var deadline = Stopwatch.StartNew();
        while (true)
        {
            var line = new List<byte>();
            while (true)
            {
                byte b = Read(1, ct, deadline)[0];
                if (b == 10)
                    break;
                if (line.Count >= 32768)
                    throw new InvalidDataException("USB response too long.");
                if (b != 13)
                    line.Add(b);
            }

            string text = Encoding.UTF8.GetString(line.ToArray());
            if (!text.StartsWith("MPOD1 "))
                continue;
            text = text[6..];
            if (text.StartsWith("ERR "))
            {
                if (ignorePriorReceiveAbort && text[4..] is "TIMEOUT_OR_DISCONNECT" or "TIMEOUT" or "ABORTED")
                    continue;
                throw new DeviceException(text[4..]);
            }
            return text;
        }

    }

    string Request(string cmd, CancellationToken ct)
    {
        var previous = activeResponseTimeout;
        activeResponseTimeout = cmd is "SCAN" or "SYNC END" or "X DBREFRESH" ? TimeSpan.FromMinutes(5) : responseTimeout;
        try
        {
            phase = "command " + string.Join(' ', cmd.Split(' ').Take(2));
            Send(cmd);
            return Reply(ct, ignorePriorReceiveAbort: cmd == "HELLO");
        }
        finally
        {
            activeResponseTimeout = previous;
        }
    }

    string X(string cmd, CancellationToken ct)
    {
        if (info.Protocol < 4)
            throw new NotSupportedException("Unsupported: this operation needs MonoPod firmware 0.10.0 / MPOD1 v4.");
        return Request("X " + cmd, ct);
    }

    static string Json(string response)
    {
        if (!response.StartsWith("OK "))
            throw new InvalidDataException(response);
        return response[3..];
    }

    static void Ok(string response, string expected = "OK DONE")
    {
        if (response != expected)
            throw new InvalidDataException("Unexpected MonoPod acknowledgement: " + response + "; expected " + expected + ".");
    }

    public Task<DeviceInfo> ConnectAsync(CancellationToken ct) => Run(c =>
    {
        if (link != null)
            throw new InvalidOperationException("This USB transport is already connected.");
        link = factory();
        try
        {
            string hello = Request("HELLO", c);
            var parts = hello.Split(' ');
            if (parts.Length < 2 || parts[0] != "READY" || !int.TryParse(parts[1], out int version) || version < 1 || version > 4)
                throw new InvalidDataException("Unsupported MonoPod protocol: " + hello);
            info = info with
            {
                Protocol = version
            };
            return ReadInfo(c);
        }
        catch
        {
            link.Dispose();
            link = null;
            throw;
        }
    }, ct);
    DeviceInfo ReadInfo(CancellationToken c)
    {
        if (info.Protocol >= 4)
        {
            var current = WireJson.Read<DeviceInfo>(Json(X("INFO", c)));
            if (current.Protocol != info.Protocol)
                throw new InvalidDataException("MonoPod INFO protocol does not match HELLO.");
            info = current;
            sync = info.SyncMode == "usb";
        }
        else
        {
            string s = Request("INFO", c);
            var pairs = s.Split(' ').Where(x => x.Contains('=')).Select(x => x.Split('=', 2)).ToDictionary(x => x[0], x => x[1]);
            long Num(string k) => pairs.TryGetValue(k, out var v) && long.TryParse(v, out long n) ? n : 0;
            info = info with
            {
                SdReady = Num("SD") == 1,
                Tracks = (int)Num("TRACKS"),
                TotalBytes = Num("TOTAL_BYTES"),
                FreeBytes = Num("FREE_BYTES"),
                Firmware = "Legacy (not reported)",
                Capabilities = ["upload", "delete", "scan"]
            };
        }

        return info;
    }

    public Task<DeviceInfo> GetDeviceInfoAsync(CancellationToken ct) => Run(ReadInfo, ct);
    public Task StartSyncAsync(CancellationToken ct) => Run(c =>
    {
        // Firmware enter(UsbCdc) is idempotent. Always confirm the current owner:
        // a previous cached session can have ended on the player or lost its SD.
        Ok(Request("SYNC START", c), "OK SYNC_STARTED");
        sync = true;
        info = info with { SyncMode = "usb" };

        return true;
    }, ct);
    public Task EndSyncAsync(CancellationToken ct) => Run(c =>
    {
        // NO_SD/SYNC_REQUIRED can invalidate our cache without releasing the
        // firmware owner. An explicit End must confirm the live device state.
        try { Ok(Request("SYNC END", c), "OK SYNC_ENDED"); }
        catch (DeviceException e) when (e.Code == "NO_USB_SYNC") { }
        sync = false;
        info = info with { SyncMode = "none" };

        return true;
    }, ct);
    public Task<IReadOnlyList<RemoteFileEntry>> ListAsync(string path, CancellationToken ct) => Run<IReadOnlyList<RemoteFileEntry>>(c =>
    {
        var result = new List<RemoteFileEntry>();
        int offset = 0;
        do
        {
            var page = WireJson.Read<DirectoryPage>(Json(X($"LIST {RemotePath.Hex(path)} {offset} 32", c)));
            result.AddRange(page.Entries);
            if (page.Next < 0)
                break;
            if (page.Next <= offset)
                throw new InvalidDataException("Invalid pagination cursor.");
            offset = page.Next;
        }
        while (true);
        return result;
    }, ct);
    public Task<RemoteFileEntry> StatAsync(string path, CancellationToken ct) => Run(c => WireJson.Read<RemoteFileEntry>(Json(X("STAT " + RemotePath.Hex(path), c))), ct);
    public Task<byte[]> ReadRangeAsync(string path, long offset, int count, CancellationToken ct) => Run(c =>
    {
        if (offset < 0 || count < 0 || count > 65536)
            throw new ArgumentOutOfRangeException(nameof(count));
        using var result = new MemoryStream();
        while (result.Length < count)
        {
            int n = Math.Min(1024, count - (int)result.Length);
            string response = X($"READ {RemotePath.Hex(path)} {offset + result.Length} {n}", c);
            var fields = response.Split(' ');
            if (fields.Length != 3 || fields[0] != "DATA" || !int.TryParse(fields[1], out int size) || size < 0 || size > n || !uint.TryParse(fields[2], System.Globalization.NumberStyles.HexNumber, null, out uint crc))
                throw new InvalidDataException("Invalid binary block header.");
            byte[] block;
            try
            {
                block = Read(size, c);
            }
            catch
            {
                Poison();
                throw;
            }

            if (Crc32.Compute(block) != crc)
            {
                Poison();
                throw new InvalidDataException("Download CRC32 mismatch.");
            }

            result.Write(block);
            if (size < n)
                break;
        }

        return result.ToArray();
    }, ct);
    void Poison()
    {
        recoveryPending |= link != null;
        poisoned = true;
        sync = false;
        link?.Dispose();
        link = null;
    }

    void Upload(Stream input, string path, long size, uint crc, bool replace, bool theme, IProgress<TransferProgress>? progress, CancellationToken ct)
    {
        if (!sync)
            throw new InvalidOperationException("Start USB Sync first.");
        if (size < 0 || size > 1073741824)
            throw new ArgumentException("File must be 0 bytes..1 GiB.");
        var watch = Stopwatch.StartNew();
        long sent = 0;
        string command;
        if (info.Protocol >= 4)
            command = $"X {(theme ? "THEME" : "PUT")} {RemotePath.Hex(path)} {size} {crc:X8} {(replace ? 1 : 0)}";
        else
        {
            if (theme || RemotePath.Parent(path) != "/Music")
                throw new NotSupportedException("Legacy USB can upload audio to /Music only.");
            command = $"{(replace ? "REPLACE" : "PUT")} {size} {crc:X8} {System.IO.Path.GetFileName(path)}";
        }

        Send(command);
        var previousTimeout = activeResponseTimeout;
        activeResponseTimeout = transferResponseTimeout;
        try
        {
            while (sent < size)
            {
                phase = $"upload {Path.GetFileName(path)}: waiting for NEXT after {sent}/{size} bytes";
                string reply = Reply(ct);
                if (!reply.StartsWith("NEXT ") || !int.TryParse(reply[5..], out int n) || n < 1 || n > 1024 || n > size - sent)
                    throw new InvalidDataException("Invalid USB block request: " + reply);
                // Once NEXT is received, cancellation closes the link; never append text to binary input.
                ct.ThrowIfCancellationRequested();
                byte[] b = new byte[n];
                input.ReadExactly(b);
                Write(b, 0, n);
                sent += n;
                progress?.Report(new(System.IO.Path.GetFileName(path), sent, size, watch.Elapsed));
            }

            activeResponseTimeout = commitResponseTimeout;
            phase = $"upload {Path.GetFileName(path)}: waiting for SAVED after {sent}/{size} bytes";
            string final = Reply(ct);
            Ok(final, "OK SAVED");
        }
        catch (DeviceException e)
        {
            // UsbSync::fail() enters a quiet/drain period after receiver errors.
            // Do not append a new command to that uncertain binary stream.
            if (e.Code is "SD_WRITE" or "TIMEOUT_OR_DISCONNECT" || sent > 0 && sent < size)
                Poison();
            throw;
        }
        catch
        {
            Poison();
            throw;
        }
        finally { activeResponseTimeout = previousTimeout; }
    }

    // Never send END into the old binary receiver. After its 15 s timeout and
    // quiet/drain interval, open a NEW link, perform HELLO/INFO, then request END.
    // This does not retry an upload or write any user file.
    public async Task RecoverAndEndSyncAsync(CancellationToken ct, IProgress<string>? progress = null)
        => await RecoverAsync(keepConnection: false, resumeSync: false, ct, progress);

    // Cancel stops the transfer queue, not the user's device connection. Once
    // the old receiver is quiet, restore the pre-cancel Sync state on a new link.
    public Task<DeviceInfo> RecoverAfterCancellationAsync(bool resumeSync, CancellationToken ct,
        IProgress<string>? progress = null)
        => RecoverAsync(keepConnection: true, resumeSync, ct, progress);

    async Task<DeviceInfo> RecoverAsync(bool keepConnection, bool resumeSync, CancellationToken ct, IProgress<string>? progress)
    {
        await gate.WaitAsync(ct);
        bool openedRecoveryLink = false;
        bool retainLink = false;
        try
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            if (!recoveryPending) return info;
            TimeSpan elapsed = lastWriteTimestamp == 0 ? TimeSpan.Zero : Stopwatch.GetElapsedTime(lastWriteTimestamp);
            TimeSpan wait = recoveryQuietPeriod - elapsed;
            if (wait > TimeSpan.Zero)
            {
                progress?.Report($"Waiting {Math.Ceiling(wait.TotalSeconds):0} s for the interrupted USB receiver to stop...");
                await Task.Delay(wait, ct);
            }
            progress?.Report(keepConnection ? "Restoring USB connection after cancellation..." : "Reconnecting USB to end Sync safely...");
            return await Task.Run(() =>
            {
                ct.ThrowIfCancellationRequested();
                link = factory();
                openedRecoveryLink = true;
                string hello = Request("HELLO", ct);
                var parts = hello.Split(' ');
                if (parts.Length < 2 || parts[0] != "READY" || !int.TryParse(parts[1], out int version) || version < 1 || version > 4)
                    throw new InvalidDataException("USB recovery did not receive a MonoPod HELLO: " + hello);
                info = info with { Protocol = version };
                ReadInfo(ct);
                if (resumeSync)
                {
                    Ok(Request("SYNC START", ct), "OK SYNC_STARTED");
                    sync = true;
                    info = info with { SyncMode = "usb" };
                }
                else
                {
                    try { Ok(Request("SYNC END", ct), "OK SYNC_ENDED"); }
                    catch (DeviceException e) when (e.Code == "NO_USB_SYNC") { }
                    sync = false;
                    info = info with { SyncMode = "none" };
                }
                if (keepConnection)
                {
                    ReadInfo(ct);
                    if (info.SyncMode != (resumeSync ? "usb" : "none"))
                        throw new InvalidDataException("USB recovery did not restore the expected Sync state.");
                    poisoned = false;
                    retainLink = true;
                }
                recoveryPending = false;
                progress?.Report(keepConnection ? "USB connection restored. Transfer cancelled; verify the target before retrying."
                    : "Sync ended. Reconnect when ready; verify the target before retrying.");
                return info;
            }, ct);
        }
        finally
        {
            try { if (openedRecoveryLink && !retainLink) link?.Dispose(); }
            finally
            {
                if (openedRecoveryLink && !retainLink) { link = null; sync = false; }
                gate.Release();
            }
        }
    }

    public async Task UploadAsync(string local, string remote, bool replace, IProgress<TransferProgress>? progress, CancellationToken ct)
    {
        uint crc = await Crc32.FileAsync(local, ct);
        await Run(c =>
        {
            using var input = File.OpenRead(local);
            Upload(input, remote, input.Length, crc, replace, false, progress, c);
            return true;
        }, ct);
    }

    public Task UploadThemeAsync(byte[] data, CancellationToken ct) => Run(c =>
    {
        using var input = new MemoryStream(data, false);
        Upload(input, "/MonoPod/theme.mpt", data.Length, Crc32.Compute(data), true, true, null, c);
        return true;
    }, ct);
    public Task DownloadAsync(string remote, string local, IProgress<TransferProgress>? progress, CancellationToken ct) => Downloads.SaveAsync(this, remote, local, progress, ct);
    public Task DeleteAsync(string path, bool directory, CancellationToken ct) => Run(c =>
    {
        if (info.Protocol < 4 && !directory && RemotePath.Parent(path) == "/Music")
            Ok(Request("DELETE " + System.IO.Path.GetFileName(path), c));
        else
            Ok(X($"{(directory ? "RMDIR" : "DELETE")} {RemotePath.Hex(path)}", c));
        return true;
    }, ct);
    public Task CreateDirectoryAsync(string path, CancellationToken ct) => Run(c =>
    {
        Ok(X("MKDIR " + RemotePath.Hex(path), c));
        return true;
    }, ct);
    public Task RenameAsync(string path, string destination, CancellationToken ct) => Run(c =>
    {
        Ok(X($"RENAME {RemotePath.Hex(path)} {RemotePath.Hex(destination)}", c));
        return true;
    }, ct);
    public Task ScanAsync(CancellationToken ct) => Run(c =>
    {
        string result = Request("SCAN", c);
        if (!result.StartsWith("OK SCANNED ", StringComparison.Ordinal) || !uint.TryParse(result[11..], out _))
            throw new InvalidDataException("Unexpected MonoPod scan acknowledgement: " + result);
        return true;
    }, ct);
    public Task SetSmoothingAsync(bool enabled, CancellationToken ct) => Run(c =>
    {
        Ok(X("SMOOTH " + (enabled ? 1 : 0), c), "OK SMOOTHING");
        return true;
    }, ct);
    public Task SetClockAsync(ClockConfiguration clock, CancellationToken ct) => Run(c =>
    {
        if (!info.Supports("clock_sync")) throw new NotSupportedException("This firmware does not support clock synchronization.");
        clock.Validate(info.Supports("ntp_server"));
        string hex = Convert.ToHexString(Encoding.ASCII.GetBytes(clock.Timezone));
        string server = info.Supports("ntp_server") ? " " + Convert.ToHexString(Encoding.ASCII.GetBytes(clock.NtpServer)) : "";
        Ok(X($"CLOCK {clock.UnixSeconds} {hex} {(clock.NtpEnabled ? 1 : 0)}{server}", c), "OK CLOCK_SET");
        return true;
    }, ct);

    public async ValueTask DisposeAsync()
    {
        await gate.WaitAsync();
        try
        {
            if (disposed)
                return;
            disposed = true;
            if (link != null && sync && !poisoned)
            {
                try
                {
                    // Explicit EndSyncAsync keeps its long rescan deadline. A local
                    // cleanup must remain bounded and never block the UI thread.
                    using var cleanup = new CancellationTokenSource(TimeSpan.FromSeconds(2));
                    await Task.Run(() => Ok(Request("SYNC END", cleanup.Token), "OK SYNC_ENDED"));
                }
                catch (IOException)
                {
                }
                catch (TimeoutException)
                {
                }
                catch (OperationCanceledException)
                {
                }

                sync = false;
            }

            link?.Dispose();
            link = null;
        }
        finally
        {
            gate.Release();
        }
    }
}
