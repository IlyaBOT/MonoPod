using Xunit;
using MonoPod.Core;

namespace MonoPod.Desktop.Tests;
public sealed class HttpTests
{
    static readonly CancellationToken CT = CancellationToken.None;
    [Fact]
    public async Task RealHttpSocketFilesAnd64BitStorage()
    {
        await using var mock = new HttpDevice();
        await using var wifi = new WiFiMonoPodTransport(mock.Url);
        var info = await wifi.ConnectAsync(CT);
        Assert.Equal(64000000000, info.TotalBytes);
        await wifi.StartSyncAsync(CT);
        var entries = await wifi.ListAsync("/Music", CT);
        Assert.Single(entries);
        Assert.Equal("Песня.mp3", entries[0].Name);
        Assert.Equal(new byte[] { 1, 2, 3, 4 }, await wifi.ReadRangeAsync(entries[0].Path, 0, 10, CT));
        await wifi.CreateDirectoryAsync("/Music/Album", CT);
        Assert.Contains("/Music/Album", mock.Folders);
        await wifi.RenameAsync(entries[0].Path, "/Music/new.mp3", CT);
        await wifi.DeleteAsync("/Music/new.mp3", false, CT);
        Assert.Empty(mock.Files);
        await wifi.EndSyncAsync(CT);
        Assert.False(mock.Sync);
    }

    [Fact]
    public async Task HttpStreamedUploadReplaceDownloadTheme()
    {
        await using var mock = new HttpDevice();
        await using var wifi = new WiFiMonoPodTransport(mock.Url);
        await wifi.ConnectAsync(CT);
        byte[] data = Enumerable.Range(0, 100000).Select(i => (byte)i).ToArray();
        string local = Path.Combine(Path.GetTempPath(), Guid.NewGuid() + ".mp3"), download = local + ".download";
        File.WriteAllBytes(local, data);
        try
        {
            await wifi.UploadAsync(local, "/Music/Песня.mp3", true, null, CT);
            Assert.Equal(data, mock.Files["/Music/Песня.mp3"]);
            await wifi.DownloadAsync("/Music/Песня.mp3", download, null, CT);
            Assert.Equal(data, File.ReadAllBytes(download));
            byte[] theme = CoreTests.SmallTheme().Encode();
            await wifi.UploadThemeAsync(theme, CT);
            Assert.Equal(theme, mock.Theme);
            await Assert.ThrowsAsync<DeviceException>(() => wifi.UploadAsync(local, "/Music/Песня.mp3", false, null, CT));
        }
        finally
        {
            File.Delete(local);
            File.Delete(download);
        }
    }

    [Fact]
    public async Task HttpPagination2100Entries()
    {
        await using var mock = new HttpDevice();
        for (int i = 0; i < 2100; i++)
            mock.Files[$"/Music/тест{i}.mp3"] = [0];
        await using var wifi = new WiFiMonoPodTransport(mock.Url);
        await wifi.ConnectAsync(CT);
        Assert.Equal(2101, (await wifi.ListAsync("/Music", CT)).Count);
        Assert.True(mock.PageCount > 64);
    }

    [Theory]
    [InlineData(404)]
    [InlineData(500)]
    public async Task HttpErrorsAreReported(int code)
    {
        await using var mock = new HttpDevice
        {
            FailCode = code
        };
        await using var wifi = new WiFiMonoPodTransport(mock.Url);
        var e = await Assert.ThrowsAsync<DeviceException>(() => wifi.ConnectAsync(CT));
        Assert.Contains(code.ToString(), e.Message);
    }

    [Fact]
    public async Task HttpSyncMustBeExplicitlyActive()
    {
        await using var mock = new HttpDevice
        {
            Sync = false
        };
        await using var wifi = new WiFiMonoPodTransport(mock.Url);
        await wifi.ConnectAsync(CT);
        await Assert.ThrowsAsync<DeviceException>(() => wifi.StartSyncAsync(CT));
    }

    [Fact]
    public async Task HttpDetectsBadCrc()
    {
        await using var mock = new HttpDevice
        {
            BadCrc = true
        };
        await using var wifi = new WiFiMonoPodTransport(mock.Url);
        await wifi.ConnectAsync(CT);
        await Assert.ThrowsAsync<InvalidDataException>(() => wifi.ReadRangeAsync("/Music/Песня.mp3", 0, 4, CT));
    }

    [Fact]
    public async Task HttpDetectsLostConnection()
    {
        await using var mock = new HttpDevice
        {
            DropRead = true
        };
        await using var wifi = new WiFiMonoPodTransport(mock.Url);
        await wifi.ConnectAsync(CT);
        await Assert.ThrowsAnyAsync<Exception>(() => wifi.ReadRangeAsync("/Music/Песня.mp3", 0, 4, CT));
    }

    [Theory]
    [InlineData("file:///tmp/device")]
    [InlineData("http://bad host")]
    [InlineData("http://user:password@device/")]
    [InlineData("http://device/api/info")]
    public void InvalidAddresses(string address) => Assert.Throws<ArgumentException>(() => new WiFiMonoPodTransport(address));
    [Fact]
    public async Task CancellationDoesNotHang()
    {
        await using var mock = new HttpDevice();
        await using var wifi = new WiFiMonoPodTransport(mock.Url);
        using var cts = new CancellationTokenSource();
        cts.Cancel();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() => wifi.ConnectAsync(cts.Token));
    }
}
