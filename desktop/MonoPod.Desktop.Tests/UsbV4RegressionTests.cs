using System.Diagnostics;
using System.Text;
using MonoPod.Core;
using Xunit;

namespace MonoPod.Desktop.Tests;

public sealed class UsbV4RegressionTests
{
    sealed class MissingSdLink : ISerialLink
    {
        public UsbDevice Device { get; } = new();
        readonly Queue<byte> error = [];
        public void Write(byte[] bytes, int offset, int count)
        {
            if (Encoding.UTF8.GetString(bytes, offset, count).StartsWith("MPOD1 X STAT ", StringComparison.Ordinal))
            {
                foreach (byte b in "MPOD1 ERR NO_SD\n"u8) error.Enqueue(b);
            }
            else Device.Write(bytes, offset, count);
        }
        public int Read(byte[] bytes, int offset, int count)
        {
            if (error.Count == 0) return Device.Read(bytes, offset, count);
            int n = Math.Min(count, error.Count);
            for (int i = 0; i < n; i++) bytes[offset+i] = error.Dequeue();
            return n;
        }
        public void Dispose() => Device.Dispose();
    }

    [Fact]
    public async Task ExplicitEndReleasesDeviceOwnerAfterSdErrorInvalidatesLocalCache()
    {
        var link = new MissingSdLink();
        await using var usb = new UsbMonoPodTransport(() => link);
        await usb.ConnectAsync(default);
        await usb.StartSyncAsync(default);
        var error = await Assert.ThrowsAsync<DeviceException>(() => usb.StatAsync("/Music/existing.mp3", default));
        Assert.Equal("NO_SD", error.Code);
        Assert.True(link.Device.Sync); // Firmware gate(NO_SD) does not call leave().
        await usb.EndSyncAsync(default);
        Assert.False(link.Device.Sync);
        Assert.Equal("MPOD1 SYNC END", link.Device.Commands[^1]);
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public async Task ThemeSmoothingAndEndFollowFirmwareV4(bool mpt2)
    {
        var device = new UsbDevice();
        await using var usb = new UsbMonoPodTransport(() => device);
        var info = await usb.ConnectAsync(default);
        Assert.Equal("0.10.6", info.Firmware);
        await usb.StartSyncAsync(default);
        byte[] theme = CoreTests.SmallTheme().Encode(mpt2);
        await usb.UploadThemeAsync(theme, default);
        Assert.Equal(theme, device.Files["/MonoPod/theme.mpt"]);
        await usb.SetSmoothingAsync(true, default);
        Assert.True(device.Smoothing);
        await usb.SetSmoothingAsync(false, default);
        Assert.False(device.Smoothing);
        await usb.EndSyncAsync(default);
        Assert.False(device.Sync);
        Assert.Contains("MPOD1 X SMOOTH 1", device.Commands);
        Assert.Contains("MPOD1 X SMOOTH 0", device.Commands);
        Assert.Equal("MPOD1 SYNC END", device.Commands[^1]);
    }

    [Fact]
    public async Task StartReacquiresSessionEvenWhenLocalCacheSaysActive()
    {
        var device = new UsbDevice();
        await using var usb = new UsbMonoPodTransport(() => device);
        await usb.ConnectAsync(default);
        await usb.StartSyncAsync(default);
        device.Sync = false;
        await usb.StartSyncAsync(default);
        Assert.True(device.Sync);
        Assert.Equal(2, device.Commands.Count(x => x == "MPOD1 SYNC START"));
        await usb.UploadThemeAsync(CoreTests.SmallTheme().Encode(), default);
        Assert.True(device.Files.ContainsKey("/MonoPod/theme.mpt"));
    }

    [Fact]
    public async Task SyncRequiredInvalidatesCacheAndAllowsAnExplicitRestart()
    {
        var device = new UsbDevice();
        await using var usb = new UsbMonoPodTransport(() => device);
        await usb.ConnectAsync(default);
        await usb.StartSyncAsync(default);
        device.Sync = false;
        var error = await Assert.ThrowsAsync<DeviceException>(() => usb.ListAsync("/Music", default));
        Assert.Equal("SYNC_REQUIRED", error.Code);
        await Assert.ThrowsAsync<InvalidOperationException>(() => usb.UploadThemeAsync(CoreTests.SmallTheme().Encode(), default));
        Assert.False(device.Disposed);
        await usb.StartSyncAsync(default);
        await usb.UploadThemeAsync(CoreTests.SmallTheme().Encode(), default);
    }

    [Fact]
    public async Task EndAcceptsAlreadyEndedDeviceSessionWithoutRepeatedCleanup()
    {
        var device = new UsbDevice();
        var usb = new UsbMonoPodTransport(() => device);
        await usb.ConnectAsync(default);
        await usb.StartSyncAsync(default);
        device.Sync = false;
        await usb.EndSyncAsync(default);
        Assert.Equal("none", (await usb.GetDeviceInfoAsync(default)).SyncMode);
        await usb.DisposeAsync();
        Assert.True(device.Disposed);
        Assert.Equal(1, device.Commands.Count(x => x == "MPOD1 SYNC END"));
    }

    [Theory]
    [InlineData("OK")]
    [InlineData("OKAY")]
    [InlineData("OK SYNC_STARTED extra")]
    public async Task MalformedStartAcknowledgementDoesNotAuthorizeAnUpload(string reply)
    {
        var device = new UsbDevice { StartReplyOverride = reply };
        await using var usb = new UsbMonoPodTransport(() => device);
        await usb.ConnectAsync(default);
        await Assert.ThrowsAsync<InvalidDataException>(() => usb.StartSyncAsync(default));
        await Assert.ThrowsAsync<IOException>(() => usb.UploadThemeAsync(CoreTests.SmallTheme().Encode(), default));
        Assert.True(device.Disposed);
        Assert.DoesNotContain(device.Commands, x => x.StartsWith("MPOD1 X THEME ", StringComparison.Ordinal));
        Assert.DoesNotContain("MPOD1 SYNC END", device.Commands);
    }

    [Fact]
    public async Task HandshakeSkipsMoreThanSixteenStartupLinesWithinItsDeadline()
    {
        var device = new UsbDevice { StartupNoiseLines = 40 };
        await using var usb = new UsbMonoPodTransport(() => device, responseTimeout: TimeSpan.FromSeconds(2));
        var info = await usb.ConnectAsync(default);
        Assert.Equal(4, info.Protocol);
        Assert.Equal("0.10.6", info.Firmware);
    }

    [Fact]
    public async Task ReceiverWriteFailureClosesBinaryStreamAndSendsNoFurtherCommands()
    {
        var device = new UsbDevice { SdWriteErrorAt = 1024 };
        var usb = new UsbMonoPodTransport(() => device);
        await usb.ConnectAsync(default);
        await usb.StartSyncAsync(default);
        string local = Path.Combine(Path.GetTempPath(), Guid.NewGuid() + ".mp3");
        await File.WriteAllBytesAsync(local, new byte[4096]);
        try
        {
            var error = await Assert.ThrowsAsync<DeviceException>(() => usb.UploadAsync(local, "/Music/existing.mp3", true, null, default));
            Assert.Equal("SD_WRITE", error.Code);
            Assert.True(device.Disposed);
            int commands = device.Commands.Count;
            await Assert.ThrowsAsync<IOException>(() => usb.GetDeviceInfoAsync(default));
            await usb.DisposeAsync();
            Assert.Equal(commands, device.Commands.Count);
            Assert.DoesNotContain("MPOD1 SYNC END", device.Commands);
            Assert.Equal(new byte[] { 1, 2, 3 }, device.Files["/Music/existing.mp3"]);
        }
        finally
        {
            await usb.DisposeAsync();
            File.Delete(local);
        }
    }

    [Theory]
    [InlineData("OK")]
    [InlineData("OK SAVED trailing")]
    public async Task UploadRequiresExactSavedAcknowledgement(string reply)
    {
        var device = new UsbDevice { FinalReplyOverride = reply };
        await using var usb = new UsbMonoPodTransport(() => device);
        await usb.ConnectAsync(default);
        await usb.StartSyncAsync(default);
        await Assert.ThrowsAsync<InvalidDataException>(() => usb.UploadThemeAsync(CoreTests.SmallTheme().Encode(), default));
        Assert.True(device.Disposed);
    }

    // Synchronous Read is intentionally waiting for the device's delayed rescan.
    // Its finite timeout models SerialPort.ReadTimeout; this catches DisposeAsync
    // accidentally running its cleanup loop inline on the caller/UI thread.
    sealed class SilentEndLink : ISerialLink
    {
        readonly UsbDevice device = new();
        readonly Stopwatch endWait = new();
        bool waitingForEnd;
        public bool Disposed { get; private set; }
        public TaskCompletionSource ReadStarted { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
        public void Write(byte[] bytes, int offset, int count)
        {
            if (Encoding.UTF8.GetString(bytes, offset, count) == "MPOD1 SYNC END\n")
            {
                waitingForEnd = true;
                endWait.Start();
            }
            else
                device.Write(bytes, offset, count);
        }
        public int Read(byte[] bytes, int offset, int count)
        {
            if (!waitingForEnd)
                return device.Read(bytes, offset, count);
            ReadStarted.TrySetResult();
            // A finite fallback also makes a regression to inline/unbounded
            // cleanup fail promptly rather than hanging the test for five minutes.
            if (endWait.Elapsed > TimeSpan.FromSeconds(5))
            {
                waitingForEnd = false;
                byte[] end = "MPOD1 SYNC END\n"u8.ToArray();
                device.Write(end, 0, end.Length);
                return device.Read(bytes, offset, count);
            }
            Thread.Sleep(50);
            throw new TimeoutException("Device rescan has not replied yet.");
        }
        public void Dispose()
        {
            Disposed = true;
            device.Dispose();
        }
    }

    [Fact]
    public async Task DisposeReturnsControlToCallerAndBoundsADeferredEndReply()
    {
        var link = new SilentEndLink();
        var usb = new UsbMonoPodTransport(() => link);
        await usb.ConnectAsync(default);
        await usb.StartSyncAsync(default);
        var watch = Stopwatch.StartNew();
        Task cleanup = usb.DisposeAsync().AsTask();
        Assert.True(watch.Elapsed < TimeSpan.FromSeconds(1), "DisposeAsync synchronously blocked its caller.");
        await link.ReadStarted.Task.WaitAsync(TimeSpan.FromSeconds(2));
        await cleanup.WaitAsync(TimeSpan.FromSeconds(6));
        Assert.True(watch.Elapsed < TimeSpan.FromSeconds(4), "USB cleanup did not honor its short local deadline.");
        Assert.True(link.Disposed);
    }
}
