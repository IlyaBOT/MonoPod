using System.Net;
using System.Security.Cryptography;
using System.Text;
using MonoPod.Core;
using Xunit;

namespace MonoPod.Desktop.Tests;

public sealed class OtaClientTests
{
    [Fact]
    public async Task ServerErrorCannotEchoTheScreenPinIntoApplicationLogMessages()
    {
        using var image=new FirmwareFixture();using var handler=new OtaHttpHandler
        {UploadStatus=HttpStatusCode.Forbidden,Acknowledgement="{\"ok\":false,\"error\":\"INVALID_PIN_654321\",\"debug\":\"654321\"}"};
        await using var wifi=new WiFiMonoPodTransport("127.0.0.1",handler);
        var error=await Assert.ThrowsAsync<OtaTransferException>(()=>wifi.UploadFirmwareAsync(image.Path,handler.Info,"654321",null,null,default));
        Assert.DoesNotContain("654321",error.Message);
    }

    [Fact]
    public async Task UploadStreamsOneFileWithExactLengthDigestAndScreenPin()
    {
        using var image = new FirmwareFixture();
        using var handler = new OtaHttpHandler();
        await using var wifi = new WiFiMonoPodTransport("127.0.0.1", handler);
        var states = new List<FirmwareUpdateState>(); var percentages = new List<double>();
        var receipt = await wifi.UploadFirmwareAsync(image.Path, handler.Info, "654321",
            new InlineProgress<TransferProgress>(p => percentages.Add(p.Percent)),
            new InlineProgress<FirmwareUpdateState>(states.Add), default);
        Assert.Equal("MP-TEST-S3", receipt.DeviceId); Assert.Equal("0.10.6.6", receipt.FirmwareBefore);
        Assert.Equal(image.Bytes.Length.ToString(), handler.SizeHeader);
        Assert.Equal(Convert.ToHexString(SHA256.HashData(image.Bytes)).ToLowerInvariant(), handler.ShaHeader);
        Assert.Equal(handler.ShaHeader, receipt.Sha256); Assert.Equal("654321", handler.PinHeader);
        Assert.Equal(handler.Body.LongLength, handler.ContentLength);
        string body = Encoding.Latin1.GetString(handler.Body);
        int start = body.IndexOf("\r\n\r\n", StringComparison.Ordinal) + 4;
        int end = body.LastIndexOf("\r\n--", StringComparison.Ordinal);
        Assert.Equal(image.Bytes, handler.Body[start..end]);
        Assert.Single(body.Split("Content-Disposition:").Skip(1));
        Assert.Contains("name=\"firmware\"; filename=\"firmware.bin\"", body);
        Assert.Contains("Content-Type: application/octet-stream", body);
        Assert.StartsWith("multipart/form-data; boundary=", handler.ContentType);
        Assert.DoesNotContain("654321", body);
        Assert.All(handler.Routes, route => Assert.DoesNotContain('?', route));
        Assert.Equal(100d, percentages.Last());
        Assert.True(states.IndexOf(FirmwareUpdateState.WaitingDeviceAck) < states.IndexOf(FirmwareUpdateState.Rebooting));
    }

    [Theory]
    [InlineData(403, "INVALID_PIN")]
    [InlineData(403, "PIN_LOCKED_RESTART_SYNC")]
    [InlineData(409, "NO_OTA_PARTITION")]
    [InlineData(409, "TRANSFER_ACTIVE")]
    [InlineData(409, "REBOOT_PENDING")]
    [InlineData(422, "SHA256_MISMATCH")]
    [InlineData(422, "FLASH_WRITE_FAILED")]
    public async Task DeviceRejectionIsExplicitAndDoesNotRetry(int code, string error)
    {
        using var image = new FirmwareFixture();
        using var handler = new OtaHttpHandler { UploadStatus = (HttpStatusCode)code,
            Acknowledgement = $"{{\"ok\":false,\"error\":\"{error}\"}}" };
        await using var wifi = new WiFiMonoPodTransport("127.0.0.1", handler);
        var result = await Assert.ThrowsAsync<OtaTransferException>(() => wifi.UploadFirmwareAsync(image.Path, handler.Info, "654321", null, null, default));
        Assert.False(result.OutcomeUncertain); Assert.Contains(error, result.Message);
        Assert.Equal(1, handler.Uploads);
        Assert.DoesNotContain("654321", result.Message);
    }

    [Theory]
    [InlineData("{\"ok\":true}")]
    [InlineData("{\"ok\":true,\"status\":\"verified\",\"rebooting\":false}")]
    [InlineData("{\"ok\":false,\"status\":\"verified\",\"rebooting\":true}")]
    [InlineData("{\"ok\":true,\"status\":7,\"rebooting\":true}")]
    [InlineData("not json")]
    public async Task SendingAllBytesIsNotAnUpdateAcknowledgement(string acknowledgement)
    {
        using var image = new FirmwareFixture();
        using var handler = new OtaHttpHandler { Acknowledgement = acknowledgement };
        await using var wifi = new WiFiMonoPodTransport("127.0.0.1", handler);
        var states = new List<FirmwareUpdateState>();
        var error = await Assert.ThrowsAsync<OtaTransferException>(() => wifi.UploadFirmwareAsync(image.Path, handler.Info, "654321", null,
            new InlineProgress<FirmwareUpdateState>(states.Add), default));
        Assert.True(error.OutcomeUncertain); Assert.Equal(image.Bytes.Length, error.BytesSent);
        Assert.DoesNotContain(FirmwareUpdateState.Rebooting, states);
    }

    [Fact]
    public async Task LostFinalResponseIsUnknownAndConnectionOwnershipIsReleased()
    {
        using var image = new FirmwareFixture();
        using var handler = new OtaHttpHandler { UploadBody = async (request, ct) =>
        {
            await request.Content!.CopyToAsync(Stream.Null, ct);
            throw new HttpRequestException("Connection closed after writing");
        } };
        await using var wifi = new WiFiMonoPodTransport("127.0.0.1", handler);
        var error = await Assert.ThrowsAsync<OtaTransferException>(() => wifi.UploadFirmwareAsync(image.Path, handler.Info, "654321", null, null, default));
        Assert.True(error.OutcomeUncertain); Assert.Contains("unknown", error.Message);
        Assert.Equal(image.Bytes.Length, error.BytesSent);
        Assert.Equal(4, (await wifi.GetDeviceInfoAsync(default)).Protocol);
    }

    [Fact]
    public async Task CancellationDuringBodyDoesNotPromiseRollback()
    {
        using var image = new FirmwareFixture(payload: 262144);
        using var handler = new OtaHttpHandler();
        await using var wifi = new WiFiMonoPodTransport("127.0.0.1", handler);
        using var stop = new CancellationTokenSource();
        var error = await Assert.ThrowsAsync<OtaTransferException>(() => wifi.UploadFirmwareAsync(image.Path, handler.Info, "654321",
            new InlineProgress<TransferProgress>(_ => stop.Cancel()), null, stop.Token));
        Assert.True(error.OutcomeUncertain); Assert.InRange(error.BytesSent, 1, image.Bytes.Length - 1);
        Assert.Contains("does not guarantee rollback", error.Message);
    }

    [Fact]
    public async Task AckTimeoutAfterCompleteBodyDoesNotClaimSuccess()
    {
        using var image = new FirmwareFixture();
        using var handler = new OtaHttpHandler { UploadBody = async (request, ct) =>
        {
            await request.Content!.CopyToAsync(Stream.Null, ct); await Task.Delay(5000, ct);
        } };
        await using var wifi = new WiFiMonoPodTransport("127.0.0.1", handler) { OtaAcknowledgementTimeout = TimeSpan.FromMilliseconds(40) };
        var error = await Assert.ThrowsAsync<OtaTransferException>(() => wifi.UploadFirmwareAsync(image.Path, handler.Info, "654321", null, null, default));
        Assert.True(error.OutcomeUncertain); Assert.Contains("timed out", error.Message);
        Assert.Equal(image.Bytes.Length, error.BytesSent);
    }

    [Fact]
    public async Task StalledUploadHitsIdleTimeout()
    {
        using var image = new FirmwareFixture();
        using var handler = new OtaHttpHandler { UploadBody = async (request, ct) =>
        { using var sink = new OtaSlowSink(5000); await request.Content!.CopyToAsync(sink, ct); } };
        await using var wifi = new WiFiMonoPodTransport("127.0.0.1", handler, transferIdleTimeout: TimeSpan.FromMilliseconds(40));
        var error = await Assert.ThrowsAsync<OtaTransferException>(() => wifi.UploadFirmwareAsync(image.Path, handler.Info, "654321", null, null, default));
        Assert.True(error.OutcomeUncertain); Assert.Contains("timed out", error.Message);
    }

    [Fact]
    public async Task ProgressResetsIdleDeadlineWithoutShortTotalTransferBudget()
    {
        using var image = new FirmwareFixture(payload: 524288);
        using var handler = new OtaHttpHandler { UploadBody = async (request, ct) =>
        { using var sink = new OtaSlowSink(5); await request.Content!.CopyToAsync(sink, ct); } };
        await using var wifi = new WiFiMonoPodTransport("127.0.0.1", handler, requestTimeout: TimeSpan.FromMilliseconds(100), transferIdleTimeout: TimeSpan.FromSeconds(1));
        var watch = System.Diagnostics.Stopwatch.StartNew();
        await wifi.UploadFirmwareAsync(image.Path, handler.Info, "654321", null, null, default);
        Assert.True(watch.Elapsed > TimeSpan.FromMilliseconds(100));
    }

    [Fact]
    public async Task FirmwareOperationExcludesSecondOtaAndFileRequests()
    {
        using var image = new FirmwareFixture();
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var finish = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        using var handler = new OtaHttpHandler { UploadBody = async (request, ct) =>
        { await request.Content!.CopyToAsync(Stream.Null, ct); entered.SetResult(); await finish.Task.WaitAsync(ct); } };
        await using var wifi = new WiFiMonoPodTransport("127.0.0.1", handler);
        await wifi.ConnectAsync(default);
        var upload = wifi.UploadFirmwareAsync(image.Path, handler.Info, "654321", null, null, default);
        await entered.Task.WaitAsync(TimeSpan.FromSeconds(3));
        try
        {
            await Assert.ThrowsAsync<InvalidOperationException>(() => wifi.UploadFirmwareAsync(image.Path, handler.Info, "654321", null, null, default));
            await Assert.ThrowsAsync<InvalidOperationException>(() => wifi.ListAsync("/", default));
            await Assert.ThrowsAsync<InvalidOperationException>(() => wifi.EndSyncAsync(default));
            Assert.Throws<IOException>(() => File.Open(image.Path, FileMode.Open, FileAccess.Write, FileShare.ReadWrite));
            Assert.Equal(1, handler.Uploads);
        }
        finally { finish.TrySetResult(); }
        await upload;
        Assert.Empty(await wifi.ListAsync("/", default));
    }

    [Fact]
    public async Task ActiveFileRequestBlocksOtaBeforeAnyFirmwarePost()
    {
        using var image = new FirmwareFixture();
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var finish = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        using var handler = new OtaHttpHandler { NormalRequest = async (_, ct) => { entered.SetResult(); await finish.Task.WaitAsync(ct); } };
        await using var wifi = new WiFiMonoPodTransport("127.0.0.1", handler);
        await wifi.ConnectAsync(default);
        var list = wifi.ListAsync("/", default); await entered.Task;
        try { await Assert.ThrowsAsync<InvalidOperationException>(() => wifi.UploadFirmwareAsync(image.Path, handler.Info, "654321", null, null, default)); }
        finally { finish.SetResult(); }
        await list; Assert.Equal(0, handler.Uploads);
    }

    [Theory]
    [InlineData("12345")]
    [InlineData("1234567")]
    [InlineData("１２３４５６")]
    [InlineData("12 456")]
    public async Task InvalidPinIsRejectedLocally(string pin)
    {
        using var image = new FirmwareFixture(); using var handler = new OtaHttpHandler();
        await using var wifi = new WiFiMonoPodTransport("127.0.0.1", handler);
        await Assert.ThrowsAsync<ArgumentException>(() => wifi.UploadFirmwareAsync(image.Path, handler.Info, pin, null, null, default));
        Assert.Empty(handler.Routes);
    }

    [Fact]
    public async Task ChangedDeviceIdBlocksUpload()
    {
        using var image = new FirmwareFixture(); using var handler = new OtaHttpHandler { Info = FirmwareFixture.Target with { DeviceId = "OTHER" } };
        await using var wifi = new WiFiMonoPodTransport("127.0.0.1", handler);
        await Assert.ThrowsAsync<InvalidDataException>(() => wifi.UploadFirmwareAsync(image.Path, FirmwareFixture.Target, "654321", null, null, default));
        Assert.Equal(0, handler.Uploads);
    }

    [Fact]
    public async Task OldFirmwareAndMissingPartitionsHaveDifferentExplanations()
    {
        using var handler = new OtaHttpHandler { InfoStatus = HttpStatusCode.NotFound };
        await using var wifi = new WiFiMonoPodTransport("127.0.0.1", handler);
        var old = await Assert.ThrowsAsync<NotSupportedException>(() => wifi.GetOtaInfoAsync(default));
        Assert.Contains("first update through USB", old.Message);
        handler.InfoStatus = HttpStatusCode.OK; handler.Info = handler.Info with { Supported = false };
        var current = await wifi.GetOtaInfoAsync(default);
        Assert.Contains("ota_0, ota_1, otadata", Assert.Throws<NotSupportedException>(current.EnsureSupported).Message);
    }

    [Fact]
    public void ReconnectChecksIdentityAndActualVersionWithoutComparingBinFilename()
    {
        var pending = new PendingOtaVerification { DeviceId = "MP-TEST-S3", FirmwareBefore = "0.10.6.6", Board = "esp32s3" };
        var live = new DeviceInfo { Firmware = "0.10.7" };
        var result = OtaReconnectionResult.Check(pending, live, FirmwareFixture.Target with { Firmware = "0.10.7" });
        Assert.True(result.SameDevice); Assert.True(result.VersionChanged); Assert.Contains("0.10.6.6 → 0.10.7", result.Message);
        Assert.False(OtaReconnectionResult.Check(pending, live, FirmwareFixture.Target with { Firmware = "0.10.7", DeviceId = "OTHER" }).SameDevice);
        result = OtaReconnectionResult.Check(pending, live with { Firmware = "0.10.6.6" }, FirmwareFixture.Target);
        Assert.False(result.VersionChanged); Assert.Contains("cannot be confirmed", result.Message);
    }
}
