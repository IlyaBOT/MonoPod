using Xunit;
using MonoPod.Core;
using MonoPod.Desktop.Themes;

namespace MonoPod.Desktop.Tests;
public sealed class CoreTests
{
    static string Fixtures => Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "docs", "theme-fixtures"));

    static readonly CancellationToken CT = CancellationToken.None;
    static string Temp(byte[] data)
    {
        string path = Path.Combine(Path.GetTempPath(), Guid.NewGuid() + ".mp3");
        File.WriteAllBytes(path, data);
        return path;
    }

    [Fact]
    public void StandardCrcVector() => Assert.Equal(0xcbf43926u, Crc32.Compute("123456789"u8));
    [Theory]
    [InlineData("/../Music")]
    [InlineData("/Music/../../x")]
    [InlineData("/Music/a\\b")]
    [InlineData("/Music/.part")]
    [InlineData("/Music/bad:*")]
    [InlineData("/Music/track. ")]
    public void RejectTraversal(string p) => Assert.Throws<ArgumentException>(() => RemotePath.Normalize(p));
    [Fact]
    public void CanonicalUnicode() => Assert.Equal("/Music/Музыка/test.mp3", RemotePath.Normalize("//Music//Музыка/test.mp3"));
    [Fact]
    public void SortingKeepsDirectoriesFirstDescending()
    {
        var input = new[]
        {
            new RemoteFileEntry
            {
                Name = "z.mp3"
            },
            new RemoteFileEntry
            {
                Name = "albums",
                Directory = true
            },
            new RemoteFileEntry
            {
                Name = "A.mp3"
            },
            new RemoteFileEntry
            {
                Name = "Z",
                Directory = true
            }
        };
        Assert.Equal(new[] { "Z", "albums", "z.mp3", "A.mp3" }, RemotePath.Sort(input, "Name", true).Select(x => x.Name));
    }

    [Theory]
    [InlineData(null, "—")]
    [InlineData(61d, "01:01")]
    [InlineData(3661d, "1:01:01")]
    [InlineData(90061d, "25:01:01")]
    public void DurationFormatting(double? seconds, string expected) => Assert.Equal(expected, DisplayValue.Duration(seconds));
    [Fact]
    public async Task UsbHandshakeSyncAnd64BitStorage()
    {
        var mock = new UsbDevice();
        await using var usb = new UsbMonoPodTransport(() => mock);
        var info = await usb.ConnectAsync(CT);
        Assert.Equal(34359738368L, info.TotalBytes);
        Assert.False(mock.Sync);
        await usb.StartSyncAsync(CT);
        Assert.True(mock.Sync);
        await usb.EndSyncAsync(CT);
        Assert.False(mock.Sync);
    }

    [Fact]
    public async Task UsbRoundTripReplaceAndNoDuplicate()
    {
        var mock = new UsbDevice();
        await using var usb = new UsbMonoPodTransport(() => mock);
        await usb.ConnectAsync(CT);
        await usb.StartSyncAsync(CT);
        byte[] bytes = Enumerable.Range(0, 9071).Select(i => (byte)i).ToArray();
        string local = Temp(bytes), download = local + ".download";
        try
        {
            await usb.UploadAsync(local, "/Music/Песня.mp3", false, null, CT);
            Assert.Equal(bytes, mock.Files["/Music/Песня.mp3"]);
            await usb.DownloadAsync("/Music/Песня.mp3", download, null, CT);
            Assert.Equal(bytes, File.ReadAllBytes(download));
            await Assert.ThrowsAsync<DeviceException>(() => usb.UploadAsync(local, "/Music/Песня.mp3", false, null, CT));
            await usb.UploadAsync(local, "/Music/Песня.mp3", true, null, CT);
            Assert.Equal(2, mock.Files.Count);
        }
        finally
        {
            File.Delete(local);
            File.Delete(download);
        }
    }

    [Fact]
    public async Task UsbCrcFailurePreservesOldFile()
    {
        var mock = new UsbDevice
        {
            BadFinalCrc = true
        };
        await using var usb = new UsbMonoPodTransport(() => mock);
        await usb.ConnectAsync(CT);
        await usb.StartSyncAsync(CT);
        string local = Temp(new byte[3011]);
        try
        {
            await Assert.ThrowsAsync<DeviceException>(() => usb.UploadAsync(local, "/Music/existing.mp3", true, null, CT));
            Assert.Equal(new byte[] { 1, 2, 3 }, mock.Files["/Music/existing.mp3"]);
        }
        finally
        {
            File.Delete(local);
        }
    }

    [Theory]
    [InlineData(true)]
    [InlineData(false)]
    public async Task UsbInterruptedBinaryNeverAppendsCommands(bool badNext)
    {
        var mock = new UsbDevice
        {
            BadNext = badNext,
            DisconnectAt = badNext ? null : 1024
        };
        var usb = new UsbMonoPodTransport(() => mock);
        await usb.ConnectAsync(CT);
        await usb.StartSyncAsync(CT);
        string local = Temp(new byte[4096]);
        try
        {
            await Assert.ThrowsAnyAsync<Exception>(() => usb.UploadAsync(local, "/Music/new.mp3", false, null, CT));
            await usb.DisposeAsync();
            Assert.DoesNotContain("MPOD1 SYNC END", mock.Commands);
            Assert.False(mock.Files.ContainsKey("/Music/new.mp3"));
        }
        finally
        {
            File.Delete(local);
        }
    }

    [Fact]
    public async Task UsbDownloadBadCrcDoesNotReplaceLocal()
    {
        var mock = new UsbDevice
        {
            BadReadCrc = true
        };
        await using var usb = new UsbMonoPodTransport(() => mock);
        await usb.ConnectAsync(CT);
        await usb.StartSyncAsync(CT);
        string local = Temp([7, 8]);
        try
        {
            await Assert.ThrowsAsync<InvalidDataException>(() => usb.DownloadAsync("/Music/existing.mp3", local, null, CT));
            Assert.Equal(new byte[] { 7, 8 }, File.ReadAllBytes(local));
            Assert.Empty(Directory.GetFiles(Path.GetDirectoryName(local)!, Path.GetFileName(local) + ".monopod-*.part"));
        }
        finally
        {
            File.Delete(local);
        }
    }

    [Fact]
    public async Task UsbPaginationOver2048()
    {
        var mock = new UsbDevice();
        for (int i = 0; i < 2100; i++)
            mock.Files[$"/Music/track{i}.mp3"] = [0];
        await using var usb = new UsbMonoPodTransport(() => mock);
        await usb.ConnectAsync(CT);
        await usb.StartSyncAsync(CT);
        Assert.Equal(2101, (await usb.ListAsync("/Music", CT)).Count);
    }

    [Fact]
    public async Task UsbNewFileCommandsAndTheme()
    {
        var mock = new UsbDevice();
        await using var usb = new UsbMonoPodTransport(() => mock);
        await usb.ConnectAsync(CT);
        await usb.StartSyncAsync(CT);
        await usb.CreateDirectoryAsync("/Music/Albums", CT);
        await usb.RenameAsync("/Music/existing.mp3", "/Music/renamed.mp3", CT);
        await usb.DeleteAsync("/Music/renamed.mp3", false, CT);
        byte[] theme = SmallTheme().Encode();
        await usb.UploadThemeAsync(theme, CT);
        Assert.Equal(theme, mock.Files["/MonoPod/theme.mpt"]);
        await usb.ScanAsync(CT);
    }

    [Fact]
    public async Task RejectWrongPort()
    {
        var mock = new UsbDevice
        {
            HelloOverride = "NOT_A_MONOPOD"
        };
        await using var usb = new UsbMonoPodTransport(() => mock);
        await Assert.ThrowsAsync<InvalidDataException>(() => usb.ConnectAsync(CT));
        Assert.True(mock.Disposed);
    }

    public static ThemeDocument SmallTheme()
    {
        var d = new ThemeDocument();
        foreach (uint cp in new uint[]
        {
            32,
            63,
            0x401,
            0x410,
            0x451
        }

        )
        {
            d.Main.Add(new(cp, 2, 3, Enumerable.Repeat((byte)0x8f, 16).ToArray()));
            d.Small.Add(new(cp, 2, 3, Enumerable.Repeat((byte)0x8f, 12).ToArray()));
        }

        return d;
    }

    [Theory]
    [InlineData(true)]
    [InlineData(false)]
    public void ThemeRoundTripAndCyrillic(bool v2)
    {
        var doc = SmallTheme();
        byte[] data = doc.Encode(v2);
        var decoded = ThemeDocument.Decode(data);
        Assert.Equal(doc.Main[3].Codepoint, decoded.Main[3].Codepoint);
        Assert.Equal(doc.Main[3].Pixels, decoded.Main[3].Pixels);
        Assert.Equal(data, decoded.Encode(v2));
    }

    [Fact]
    public void ThemeRgb565Vector()
    {
        Assert.Equal(0xf800, ThemeDocument.Rgb565(255, 0, 0));
        Assert.Equal(0x07e0, ThemeDocument.Rgb565(0, 255, 0));
        Assert.Equal((255, 0, 0), ThemeDocument.Rgb888(0xf800));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(4)]
    [InlineData(36)]
    [InlineData(40)]
    [InlineData(51)]
    public void ThemeRejectsDamage(int at)
    {
        var data = SmallTheme().Encode();
        data[at] ^= 0xff;
        Assert.Throws<InvalidDataException>(() => ThemeDocument.Decode(data));
    }

    [Fact]
    public void ThemeRejectsInvalidGlyphEvenWithValidCrc()
    {
        var data = SmallTheme().Encode();
        data[48] = 255;
        System.Buffers.Binary.BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(40), Crc32.Compute(data.AsSpan(44)));
        Assert.Throws<InvalidDataException>(() => ThemeDocument.Decode(data));
    }

    [Fact]
    public void ThemeRejectsTruncation()
    {
        var bytes = SmallTheme().Encode();
        foreach (int length in new[]
        {
            0,
            27,
            28,
            43,
            bytes.Length - 1
        }

        )
            Assert.Throws<InvalidDataException>(() => ThemeDocument.Decode(bytes[..length]));
    }

    [Fact]
    public void RasterCyrillicAndSmoothingPreview()
    {
        var profile = new ThemeProfile
        {
            FontPath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Fonts), "arial.ttf")
        };
        var doc = ThemeRasterizer.Create(profile);
        Assert.Contains(doc.Main, g => g.Codepoint == 0x451);
        Directory.CreateDirectory(Fixtures);
        File.WriteAllBytes(Path.Combine(Fixtures, "csharp-mpt1.mpt"), doc.Encode(false));
        File.WriteAllBytes(Path.Combine(Fixtures, "csharp-mpt2.mpt"), doc.Encode());
        using (var example = ThemeRasterizer.Preview(doc, true, 0))
            example.Save(Path.Combine(Fixtures, "menu.png"));
        Assert.Contains(doc.Main, g => g.Pixels.Any(b => (b & 15)is> 0 and < 15 || (b >> 4)is> 0 and < 15));
        using var off = ThemeRasterizer.Preview(doc, false, 0);
        using var on = ThemeRasterizer.Preview(doc, true, 0);
        Assert.Equal(new System.Drawing.Size(160, 128), off.Size);
        Assert.Contains(Enumerable.Range(0, 160 * 128), i => off.GetPixel(i % 160, i / 160) != on.GetPixel(i % 160, i / 160));
    }

    [Fact]
    public async Task EmptyMetadataDoesNotInventTagsOrDuration()
    {
        byte[] bytes = new byte[10000];
        var m = await MetadataReader.ReadAsync("famous-song.mp3", bytes.Length, (offset, n) => Task.FromResult(bytes.Skip((int)offset).Take(n).ToArray()), CT);
        Assert.Equal("", m.Title);
        Assert.Null(m.Seconds);
    }

    [Fact]
    public async Task WavDurationComesFromDataNotWholeFileSize()
    {
        using var stream = new MemoryStream();
        using (var w = new BinaryWriter(stream, System.Text.Encoding.ASCII, true))
        {
            w.Write("RIFF"u8);
            w.Write(16036);
            w.Write("WAVEfmt "u8);
            w.Write(16);
            w.Write((ushort)1);
            w.Write((ushort)1);
            w.Write(8000);
            w.Write(16000);
            w.Write((ushort)2);
            w.Write((ushort)16);
            w.Write("data"u8);
            w.Write(16000);
            w.Write(new byte[16000]);
        }

        byte[] bytes = stream.ToArray();
        long read = 0;
        var m = await MetadataReader.ReadAsync("test.wav", bytes.Length, (offset, n) =>
        {
            read += n;
            return Task.FromResult(bytes.Skip((int)offset).Take(n).ToArray());
        }, CT);
        Assert.Equal(1d, m.Seconds);
        Assert.Equal(8000, m.SampleRate);
        Assert.True(read < 100);
    }
}
