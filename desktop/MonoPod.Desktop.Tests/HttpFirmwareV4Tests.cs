using System.Net;
using System.Text.Json;
using MonoPod.Core;
using Xunit;

namespace MonoPod.Desktop.Tests;

public sealed class HttpFirmwareV4Tests
{
    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public async Task ThemeUsesDedicatedV4EndpointAndWholeFileCrc(bool mpt2)
    {
        await using var device = new HttpDevice();
        device.Files["/MonoPod/theme.mpt"] = [7, 8, 9];
        await using var wifi = new WiFiMonoPodTransport(device.Url);
        Assert.Equal("0.10.6", (await wifi.ConnectAsync(default)).Firmware);

        byte[] theme = CoreTests.SmallTheme().Encode(mpt2);
        // Palette is outside MPT's payload CRC, but must be included in transfer CRC.
        theme[12] ^= 0x31;
        await wifi.UploadThemeAsync(theme, default);

        Assert.Equal(theme, device.Theme);
        Assert.Equal(theme, device.Files["/MonoPod/theme.mpt"]);
        var upload = Assert.Single(device.Requests, r => r.Method == "POST");
        Assert.Equal("/api/theme", upload.Path);
        Assert.Contains($"size={theme.Length}", upload.Query);
        Assert.Contains($"crc={Crc32.Compute(theme):X8}", upload.Query);
        Assert.Contains("replace=1", upload.Query);
        Assert.DoesNotContain("theme=1", upload.Query);
    }

    [Fact]
    public async Task GenericUploadThemeFlagCannotBypassFirmwareProtection()
    {
        await using var device = new HttpDevice();
        byte[] theme = CoreTests.SmallTheme().Encode();
        using var http = new HttpClient { BaseAddress = new Uri(device.Url) };
        using var body = new MultipartFormDataContent();
        body.Add(new ByteArrayContent(theme), "theme", "theme.mpt");
        using var response = await http.PostAsync(
            $"api/fs/upload?theme=1&path=%2FMonoPod%2Ftheme.mpt&size={theme.Length}&crc={Crc32.Compute(theme):X8}&replace=1", body);

        Assert.Equal(HttpStatusCode.UnprocessableEntity, response.StatusCode);
        Assert.Equal("PROTECTED_PATH", await response.Content.ReadAsStringAsync());
        Assert.Null(device.Theme);
        Assert.False(device.Files.ContainsKey("/MonoPod/theme.mpt"));
    }

    [Fact]
    public async Task InvalidThemeIsRejectedBeforeSendingAFile()
    {
        await using var device = new HttpDevice();
        await using var wifi = new WiFiMonoPodTransport(device.Url);
        await wifi.ConnectAsync(default);
        byte[] broken = CoreTests.SmallTheme().Encode();
        broken[^1] ^= 1;

        await Assert.ThrowsAsync<InvalidDataException>(() => wifi.UploadThemeAsync(broken, default));
        Assert.DoesNotContain(device.Requests, r => r.Method == "POST");
    }

    [Fact]
    public async Task StartSyncChecksCurrentPlayerSessionInsteadOfCachedInfo()
    {
        await using var device = new HttpDevice();
        await using var wifi = new WiFiMonoPodTransport(device.Url);
        await wifi.ConnectAsync(default);
        device.Sync = false; // User left Wi-Fi Sync on the player after Connect.

        await Assert.ThrowsAsync<DeviceException>(() => wifi.StartSyncAsync(default));
        Assert.Equal(2, device.Requests.Count(r => r.Path == "/api/info"));
        Assert.DoesNotContain(device.Requests, r => r.Path.Contains("start"));
    }

    [Fact]
    public async Task StartSyncReportsMissingSdBeforeAnyFileRequest()
    {
        await using var device = new HttpDevice();
        await using var wifi = new WiFiMonoPodTransport(device.Url);
        await wifi.ConnectAsync(default);
        device.SdReady = false;

        var error = await Assert.ThrowsAsync<DeviceException>(() => wifi.StartSyncAsync(default));
        Assert.Contains("NO_SD", error.Message);
        Assert.All(device.Requests, r => Assert.Equal("/api/info", r.Path));
    }

    sealed class AcknowledgementHandler(string acknowledgement) : HttpMessageHandler
    {
        protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken ct)
        {
            if (request.RequestUri!.AbsolutePath == "/api/info")
                return new(HttpStatusCode.OK)
                {
                    Content = new StringContent(JsonSerializer.Serialize(new DeviceInfo
                    {
                        Firmware = "0.10.6", Protocol = 4, SdReady = true,
                        SyncMode = "wifi", Capabilities = ["theme", "fs_upload"]
                    }, WireJson.Options))
                };
            await request.Content!.CopyToAsync(Stream.Null, ct);
            return new(HttpStatusCode.OK) { Content = new StringContent(acknowledgement) };
        }
    }

    [Theory]
    [InlineData("{\"ok\":false}")]
    [InlineData("{}")]
    [InlineData("<html>Captive portal</html>")]
    [InlineData("saved")]
    public async Task Http200WithoutFirmwareAcknowledgementIsNotSuccess(string reply)
    {
        await using var wifi = new WiFiMonoPodTransport("http://127.0.0.1", new AcknowledgementHandler(reply));
        await wifi.ConnectAsync(default);
        await Assert.ThrowsAsync<InvalidDataException>(() => wifi.UploadThemeAsync(CoreTests.SmallTheme().Encode(), default));
    }
}
