using System.Text;
using MonoPod.Core;
using Xunit;

namespace MonoPod.Desktop.Tests;

public sealed class UsbBavHotfixTests
{
    [Fact]
    public void PacingPreservesLogicalBlockAndDoesNotOverflow256ByteQueueDuring24msPause()
    {
        byte[] source = Enumerable.Range(0, 1024).Select(n => (byte)n).ToArray();
        // HWCDC ISR drains the USB FIFO into a finite queue even while the
        // firmware loop is rendering. Overflowed bytes do not reach MPOD1.
        int oldQueued = 0, oldDropped = 0;
        foreach (byte b in source) { if (oldQueued < 256) oldQueued++; else oldDropped++; }
        Assert.Equal(768, oldDropped);

        var queue = new Queue<byte>();
        var received = new List<byte>();
        var writes = new List<int>();
        int milliseconds = 0, dropped = 0;
        void Service()
        {
            if (milliseconds < 24) return;
            while (queue.TryDequeue(out byte b)) received.Add(b);
        }
        UsbPacketWriter.Write(source, 0, source.Length, (buffer, offset, count) =>
        {
            writes.Add(count);
            Service();
            for (int i = 0; i < count; i++)
            {
                if (queue.Count < 256) queue.Enqueue(buffer[offset+i]); else dropped++;
            }
        }, interval => { milliseconds += interval; Service(); });
        milliseconds += 24; Service();
        Assert.Equal(0, dropped);
        Assert.Equal(source, received);
        Assert.Equal(16, writes.Count);
        Assert.All(writes, count => Assert.InRange(count, 1, 64));
        Assert.True(milliseconds >= 24 + 15 * UsbPacketWriter.PauseMilliseconds);
    }

    [Fact]
    public void PacketWriterHonorsOffsetAndShortTailWithoutAddingBytes()
    {
        byte[] input = Enumerable.Range(0, 200).Select(i => (byte)i).ToArray();
        var written = new List<byte>();
        var sizes = new List<int>();
        int pauses = 0;
        UsbPacketWriter.Write(input, 7, 130, (b,o,n) => { sizes.Add(n); written.AddRange(b.Skip(o).Take(n)); }, _ => pauses++);
        Assert.Equal(new[]{64,64,2}, sizes);
        Assert.Equal(input.Skip(7).Take(130), written);
        Assert.Equal(2, pauses);
    }

    static async Task BreakReceiver(UsbMonoPodTransport usb)
    {
        await usb.ConnectAsync(default);
        await usb.StartSyncAsync(default);
        await Assert.ThrowsAsync<DeviceException>(() => usb.UploadThemeAsync(new byte[4096], default));
        Assert.True(usb.NeedsRecovery);
    }

    [Fact]
    public async Task InterruptedUploadEndsSyncOnFreshConnectionWithoutRetryingAFile()
    {
        var original = new UsbDevice { SdWriteErrorAt = 1024 };
        var fresh = new UsbDevice { Sync = true };
        int opened = 0;
        await using var usb = new UsbMonoPodTransport(() => ++opened == 1 ? original : fresh,
            recoveryQuietPeriod: TimeSpan.Zero);
        await BreakReceiver(usb);
        Assert.True(original.Disposed);
        Assert.DoesNotContain("MPOD1 SYNC END", original.Commands);
        await usb.RecoverAndEndSyncAsync(default);
        Assert.Equal(2, opened);
        Assert.False(fresh.Sync);
        Assert.True(fresh.Disposed);
        Assert.False(usb.NeedsRecovery);
        Assert.Equal(new[]{"MPOD1 HELLO","MPOD1 X INFO","MPOD1 SYNC END"},fresh.Commands);
        Assert.Equal(new byte[]{1,2,3}, original.Files["/Music/existing.mp3"]);
    }

    [Fact]
    public async Task QuietWaitCanBeCancelledWithoutSendingCommandsIntoOldReceiver()
    {
        var original = new UsbDevice { SdWriteErrorAt = 1024 };
        int opened = 0;
        await using var usb = new UsbMonoPodTransport(() => { opened++; return original; },
            recoveryQuietPeriod: TimeSpan.FromSeconds(5));
        await BreakReceiver(usb);
        int commands = original.Commands.Count;
        using var stop = new CancellationTokenSource(TimeSpan.FromMilliseconds(40));
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() => usb.RecoverAndEndSyncAsync(stop.Token));
        Assert.Equal(1, opened);
        Assert.Equal(commands, original.Commands.Count);
    }

    [Fact]
    public async Task RecoveryDoesNotTouchHealthyConnection()
    {
        var device = new UsbDevice();
        await using var usb = new UsbMonoPodTransport(() => device);
        await usb.ConnectAsync(default);
        await usb.StartSyncAsync(default);
        int commands = device.Commands.Count;
        await usb.RecoverAndEndSyncAsync(default);
        Assert.Equal(commands, device.Commands.Count);
        Assert.False(device.Disposed);
        Assert.True(device.Sync);
    }

    [Fact]
    public async Task RecoveryDoesNotSendEndToUnexpectedDevice()
    {
        var original = new UsbDevice { SdWriteErrorAt = 1024 };
        var wrong = new UsbDevice { HelloOverride = "READY 99 SD=1" };
        int opened = 0;
        await using var usb = new UsbMonoPodTransport(() => ++opened == 1 ? original : wrong,
            recoveryQuietPeriod: TimeSpan.Zero);
        await BreakReceiver(usb);
        await Assert.ThrowsAsync<InvalidDataException>(() => usb.RecoverAndEndSyncAsync(default));
        Assert.DoesNotContain("MPOD1 SYNC END", wrong.Commands);
        Assert.True(wrong.Disposed);
        Assert.True(usb.NeedsRecovery);
    }

    sealed class StaleAbort : ISerialLink
    {
        readonly UsbDevice inner = new();
        readonly Queue<byte> stale = new();
        public int Read(byte[] b,int o,int n)
        {
            if (stale.Count==0) return inner.Read(b,o,n);
            int count=Math.Min(stale.Count,n);
            for(int i=0;i<count;i++)b[o+i]=stale.Dequeue();
            return count;
        }
        public void Write(byte[] b,int o,int n)
        {
            if (Encoding.UTF8.GetString(b,o,n)=="MPOD1 HELLO\n")
                foreach(byte c in "MPOD1 ERR TIMEOUT_OR_DISCONNECT\n"u8)stale.Enqueue(c);
            inner.Write(b,o,n);
        }
        public void Dispose()=>inner.Dispose();
    }

    [Fact]
    public async Task HandshakeAcceptsReadyAfterPriorReceiverAbort()
    {
        await using var usb=new UsbMonoPodTransport(()=>new StaleAbort());
        Assert.Equal("0.10.6",(await usb.ConnectAsync(default)).Firmware);
    }
}
