using Xunit;
using MonoPod.Core;

namespace MonoPod.Desktop.Tests;
public sealed class ReconnectTests
{
    [Fact]
    public async Task ReconnectAdoptsExistingUsbSyncAndCanEndNormally()
    {
        var mock = new UsbDevice
        {
            Sync = true
        };
        var usb = new UsbMonoPodTransport(() => mock);
        var info = await usb.ConnectAsync(default);
        Assert.Equal("usb", info.SyncMode);
        Assert.DoesNotContain("MPOD1 SYNC START", mock.Commands);
        await usb.DisposeAsync();
        Assert.False(mock.Sync);
        Assert.Contains("MPOD1 SYNC END", mock.Commands);
    }
}
