using Xunit;
using MonoPod.Core;
using System.Text;

namespace MonoPod.Desktop.Tests;
public sealed class EdgeTests
{
    static readonly CancellationToken CT = CancellationToken.None;
    sealed class Silent : ISerialLink
    {
        public bool Closed;
        public int Read(byte[] b, int o, int n) => throw new TimeoutException();
        public void Write(byte[] b, int o, int n)
        {
        }

        public void Dispose() => Closed = true;
    }

    sealed class ImmediateProgress(Action<TransferProgress> action) : IProgress<TransferProgress>
    {
        public void Report(TransferProgress p) => action(p);
    }

    [Fact]
    public async Task UsbTimeoutIsBounded()
    {
        var link = new Silent();
        await using var usb = new UsbMonoPodTransport(() => link, responseTimeout: TimeSpan.FromMilliseconds(30));
        var watch = System.Diagnostics.Stopwatch.StartNew();
        await Assert.ThrowsAsync<TimeoutException>(() => usb.ConnectAsync(CT));
        Assert.True(link.Closed);
        Assert.True(watch.Elapsed < TimeSpan.FromSeconds(3));
    }

    [Fact]
    public async Task UsbCancelAfterFirstBlockPreservesDestinationAndClosesLink()
    {
        var mock = new UsbDevice();
        var usb = new UsbMonoPodTransport(() => mock);
        await usb.ConnectAsync(CT);
        await usb.StartSyncAsync(CT);
        string file = Path.GetTempFileName();
        File.WriteAllBytes(file, new byte[16000]);
        using var cancel = new CancellationTokenSource();
        try
        {
            await Assert.ThrowsAnyAsync<OperationCanceledException>(() => usb.UploadAsync(file, "/Music/existing.mp3", true, new ImmediateProgress(_ => cancel.Cancel()), cancel.Token));
            await usb.DisposeAsync();
            Assert.Equal(new byte[] { 1, 2, 3 }, mock.Files["/Music/existing.mp3"]);
            Assert.DoesNotContain("MPOD1 SYNC END", mock.Commands);
            Assert.True(mock.Disposed);
        }
        finally
        {
            File.Delete(file);
        }
    }

    [Fact]
    public async Task UsbRetryAfterCrcFailureCommitsOneFile()
    {
        var mock = new UsbDevice
        {
            BadFinalCrc = true
        };
        await using var usb = new UsbMonoPodTransport(() => mock);
        await usb.ConnectAsync(CT);
        await usb.StartSyncAsync(CT);
        string file = Path.GetTempFileName();
        File.WriteAllBytes(file, new byte[5000]);
        try
        {
            await Assert.ThrowsAsync<DeviceException>(() => usb.UploadAsync(file, "/Music/existing.mp3", true, null, CT));
            mock.BadFinalCrc = false;
            await usb.UploadAsync(file, "/Music/existing.mp3", true, null, CT);
            Assert.Single(mock.Files);
            Assert.Equal(5000, mock.Files["/Music/existing.mp3"].Length);
        }
        finally
        {
            File.Delete(file);
        }
    }

    [Fact]
    public async Task UsbZeroLengthFile()
    {
        var mock = new UsbDevice();
        await using var usb = new UsbMonoPodTransport(() => mock);
        await usb.ConnectAsync(CT);
        await usb.StartSyncAsync(CT);
        string file = Path.GetTempFileName();
        try
        {
            await usb.UploadAsync(file, "/Music/empty.txt", false, null, CT);
            Assert.Empty(mock.Files["/Music/empty.txt"]);
        }
        finally
        {
            File.Delete(file);
        }
    }

    [Fact]
    public async Task EmptyDirectory()
    {
        await using var mock = new HttpDevice();
        mock.Folders.Add("/Empty");
        await using var wifi = new WiFiMonoPodTransport(mock.Url);
        await wifi.ConnectAsync(CT);
        Assert.Empty(await wifi.ListAsync("/Empty", CT));
    }

    [Theory]
    [InlineData(89, true)]
    [InlineData(91, false)]
    public void PathUtf8ByteLength(int count, bool valid)
    {
        string p = "/Music/" + new string ('a', count);
        if (valid)
            Assert.Equal(p, RemotePath.Normalize(p));
        else
            Assert.Throws<ArgumentException>(() => RemotePath.Normalize(p));
    }

    [Fact]
    public void RootParentNeverEscapes() => Assert.Equal("/", RemotePath.Parent("/"));
    [Fact]
    public async Task Id3Utf16TagsAndTlen()
    {
        using var payload = new MemoryStream();
        void Frame(string id, string text)
        {
            byte[] v = Encoding.Unicode.GetBytes("\ufeff" + text);
            byte[] n = BitConverter.GetBytes(System.Net.IPAddress.HostToNetworkOrder(v.Length + 1));
            payload.Write(Encoding.ASCII.GetBytes(id));
            payload.Write(n);
            payload.Write(new byte[2]);
            payload.WriteByte(1);
            payload.Write(v);
        }

        Frame("TIT2", "Песня");
        Frame("TPE1", "Исполнитель");
        Frame("TALB", "Альбом");
        Frame("TPE2", "Группа");
        Frame("TRCK", "2/12");
        Frame("TLEN", "123456");
        int length = (int)payload.Length;
        using var file = new MemoryStream();
        file.Write("ID3"u8);
        file.Write(new byte[] { 3, 0, 0, (byte)(length >> 21), (byte)((length >> 14) & 127), (byte)((length >> 7) & 127), (byte)(length & 127) });
        file.Write(payload.ToArray());
        file.Write(new byte[500000]);
        byte[] bytes = file.ToArray();
        long transferred = 0;
        var m = await MetadataReader.ReadAsync("wrong-name.mp3", bytes.Length, (offset, n) =>
        {
            transferred += n;
            return Task.FromResult(bytes.Skip((int)offset).Take(n).ToArray());
        }, CT);
        Assert.Equal("Песня", m.Title);
        Assert.Equal("Исполнитель", m.Artist);
        Assert.Equal("Группа", m.AlbumArtist);
        Assert.Equal(123.456, m.Seconds);
        Assert.True(transferred < 10000);
    }

    [Fact]
    public async Task FlacDurationSampleCountAndVorbisTags()
    {
        using var file = new MemoryStream();
        file.Write("fLaC"u8);
        file.Write(new byte[] { 0, 0, 0, 34 });
        byte[] si = new byte[34];
        ulong packed = (48000UL << 44) | (1UL << 41) | (15UL << 36) | 96000UL;
        System.Buffers.Binary.BinaryPrimitives.WriteUInt64BigEndian(si.AsSpan(10, 8), packed);
        file.Write(si);
        using var c = new MemoryStream();
        using (var w = new BinaryWriter(c, Encoding.UTF8, true))
        {
            w.Write(0);
            w.Write(3);
            foreach (string s in new[]
            {
                "TITLE=Трек",
                "ARTIST=Artist",
                "ALBUMARTIST=Band"
            }

            )
            {
                byte[] b = Encoding.UTF8.GetBytes(s);
                w.Write(b.Length);
                w.Write(b);
            }
        }

        int n = (int)c.Length;
        file.Write(new byte[] { 0x84, (byte)(n >> 16), (byte)(n >> 8), (byte)n });
        file.Write(c.ToArray());
        file.Write(new byte[1000000]);
        byte[] data = file.ToArray();
        var m = await MetadataReader.ReadAsync("track.flac", data.Length, (offset, count) => Task.FromResult(data.Skip((int)offset).Take(count).ToArray()), CT);
        Assert.Equal(2d, m.Seconds);
        Assert.Equal(48000, m.SampleRate);
        Assert.Equal("Трек", m.Title);
        Assert.Equal("Band", m.AlbumArtist);
    }

    [Fact]
    public async Task CbrMp3DoesNotInventDuration()
    {
        byte[] data = new byte[10000];
        data[0] = 255;
        data[1] = 251;
        data[2] = 144;
        data[3] = 0;
        var m = await MetadataReader.ReadAsync("test.mp3", data.Length, (offset, count) => Task.FromResult(data.Skip((int)offset).Take(count).ToArray()), CT);
        Assert.Equal(128, m.Bitrate);
        Assert.Equal(44100, m.SampleRate);
        Assert.Null(m.Seconds);
    }

    [Fact]
    public void Every565ColorRoundTripsThroughBitReplication()
    {
        for (int i = 0; i < 65536; i++)
        {
            int r = (i >> 11) & 31, g = (i >> 5) & 63, b = i & 31;
            Assert.Equal((ushort)i, ThemeDocument.Rgb565((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)));
        }
    }
}
