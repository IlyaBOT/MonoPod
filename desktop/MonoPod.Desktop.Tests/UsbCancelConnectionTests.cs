using MonoPod.Core;
using Xunit;

namespace MonoPod.Desktop.Tests;

public sealed class UsbCancelConnectionTests
{
    sealed class CancelProgress(CancellationTokenSource stop) : IProgress<TransferProgress>
    {
        public void Report(TransferProgress progress) => stop.Cancel();
    }

    [Fact]
    public async Task CancelledUploadReconnectsAndCanTransferAnotherFileWithoutManualConnect()
    {
        var original = new UsbDevice();
        var fresh = new UsbDevice { Sync = true };
        int opens = 0;
        await using var usb = new UsbMonoPodTransport(() => ++opens == 1 ? original : fresh,
            recoveryQuietPeriod: TimeSpan.Zero);
        await usb.ConnectAsync(default);
        await usb.StartSyncAsync(default);
        string local = Path.GetTempFileName();
        try
        {
            await File.WriteAllBytesAsync(local, new byte[4096]);
            using var stop = new CancellationTokenSource();
            await Assert.ThrowsAnyAsync<OperationCanceledException>(() => usb.UploadAsync(local,
                "/Music/existing.mp3", true, new CancelProgress(stop), stop.Token));
            Assert.True(original.Disposed);
            Assert.True(usb.NeedsRecovery);
            Assert.Equal(new byte[]{1,2,3}, original.Files["/Music/existing.mp3"]);

            var info = await usb.RecoverAfterCancellationAsync(resumeSync: true, default);
            Assert.Equal("usb", info.SyncMode);
            Assert.False(fresh.Disposed);
            Assert.False(usb.NeedsRecovery);
            Assert.Equal(2, opens);
            Assert.DoesNotContain("MPOD1 SYNC END", original.Commands);
            Assert.DoesNotContain("MPOD1 SYNC END", fresh.Commands);
            Assert.DoesNotContain(fresh.Commands, c => c.StartsWith("MPOD1 X PUT ", StringComparison.Ordinal));

            Assert.Single(await usb.ListAsync("/Music", default));
            await usb.UploadAsync(local, "/Music/next.mp3", false, null, default);
            Assert.Equal(4096, fresh.Files["/Music/next.mp3"].Length);
            Assert.Equal(new byte[]{1,2,3}, fresh.Files["/Music/existing.mp3"]);
            await usb.EndSyncAsync(default);
            Assert.False(fresh.Sync);
        }
        finally { File.Delete(local); }
    }

    [Fact]
    public async Task ReconnectCanPreserveConnectedButInactiveSyncState()
    {
        var original = new UsbDevice { SdWriteErrorAt = 1024 };
        var fresh = new UsbDevice { Sync = true };
        int opens = 0;
        await using var usb = new UsbMonoPodTransport(() => ++opens == 1 ? original : fresh,
            recoveryQuietPeriod: TimeSpan.Zero);
        await usb.ConnectAsync(default);
        await usb.StartSyncAsync(default);
        await Assert.ThrowsAsync<DeviceException>(() => usb.UploadThemeAsync(new byte[4096], default));
        var info = await usb.RecoverAfterCancellationAsync(resumeSync: false, default);
        Assert.Equal("none", info.SyncMode);
        Assert.False(fresh.Disposed);
        Assert.False(fresh.Sync);
        Assert.Equal(4, (await usb.GetDeviceInfoAsync(default)).Protocol);
        Assert.DoesNotContain("MPOD1 SYNC START", fresh.Commands);
        Assert.Contains("MPOD1 SYNC END", fresh.Commands);
    }

    [Fact]
    public async Task HealthyCancellationRecoveryDoesNotReopenOrEndTheSession()
    {
        var device = new UsbDevice { Sync = true };
        int opens = 0;
        await using var usb = new UsbMonoPodTransport(() => { opens++; return device; });
        await usb.ConnectAsync(default);
        int commands = device.Commands.Count;
        var info = await usb.RecoverAfterCancellationAsync(resumeSync: true, default);
        Assert.Equal(1, opens);
        Assert.Equal(commands, device.Commands.Count);
        Assert.False(device.Disposed);
        Assert.Equal("usb", info.SyncMode);
    }

    [Fact]
    public async Task FailedCancellationReconnectClosesFreshPortAndDoesNotClaimRecovery()
    {
        var original = new UsbDevice { SdWriteErrorAt = 1024 };
        var wrong = new UsbDevice { HelloOverride = "READY 99 SD=1" };
        int opens = 0;
        await using var usb = new UsbMonoPodTransport(() => ++opens == 1 ? original : wrong,
            recoveryQuietPeriod: TimeSpan.Zero);
        await usb.ConnectAsync(default);
        await usb.StartSyncAsync(default);
        await Assert.ThrowsAsync<DeviceException>(() => usb.UploadThemeAsync(new byte[4096], default));
        await Assert.ThrowsAsync<InvalidDataException>(() => usb.RecoverAfterCancellationAsync(true, default));
        Assert.True(usb.NeedsRecovery);
        Assert.True(wrong.Disposed);
        Assert.DoesNotContain("MPOD1 SYNC START", wrong.Commands);
        await Assert.ThrowsAsync<IOException>(() => usb.GetDeviceInfoAsync(default));
    }
}
