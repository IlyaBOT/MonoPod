using Xunit;
using MonoPod.Core;
using System.Net;
using System.Text;

namespace MonoPod.Desktop.Tests;
public sealed class LegacyTests
{
    sealed class LegacyHttp : HttpMessageHandler
    {
        public string? UploadedRoute;
        public byte[]? UploadedBody;
        protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken ct)
        {
            if (request.RequestUri!.AbsolutePath == "/api/info")
                return new(HttpStatusCode.OK)
                {
                    Content = new StringContent("{\"firmware\":\"MonoPod v0.9.1.1\",\"sd_ready\":true,\"tracks\":5,\"sd_total_mb\":32768,\"sd_used_mb\":1024}")
                };
            UploadedRoute = request.RequestUri.AbsolutePath;
            UploadedBody = await request.Content!.ReadAsByteArrayAsync(ct);
            return new(HttpStatusCode.OK)
            {
                Content = new StringContent("saved")
            };
        }
    }

    [Fact]
    public async Task LegacyWifiUploadAndThemeRemainUsable()
    {
        var handler = new LegacyHttp();
        await using var wifi = new WiFiMonoPodTransport("http://127.0.0.1", handler);
        var info = await wifi.ConnectAsync(default);
        Assert.Equal(32768L * 1048576, info.TotalBytes);
        Assert.False(info.Supports("list"));
        string file = Path.GetTempFileName();
        File.WriteAllBytes(file, "test audio bytes"u8.ToArray());
        try
        {
            await wifi.StartSyncAsync(default);
            await wifi.UploadAsync(file, "/Music/song.mp3", true, null, default);
            Assert.Equal("/upload", handler.UploadedRoute);
            Assert.Contains("test audio bytes", Encoding.UTF8.GetString(handler.UploadedBody!));
            await wifi.UploadThemeAsync(CoreTests.SmallTheme().Encode(true), default);
            Assert.Equal("/api/theme", handler.UploadedRoute);
            Assert.Contains("MPT1", Encoding.UTF8.GetString(handler.UploadedBody!));
            await Assert.ThrowsAsync<NotSupportedException>(() => wifi.ListAsync("/Music", default));
        }
        finally
        {
            File.Delete(file);
        }
    }
}
