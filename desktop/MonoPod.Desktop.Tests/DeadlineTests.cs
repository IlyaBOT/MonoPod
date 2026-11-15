using Xunit;
using MonoPod.Core;
using System.Text;

namespace MonoPod.Desktop.Tests;
public sealed class DeadlineTests
{
    sealed class Trickle : ISerialLink
    {
        readonly Queue<byte> bytes = [];
        public void Write(byte[] b, int o, int n)
        {
            foreach (byte c in Encoding.UTF8.GetBytes("MPOD1 READY 4 SD=1\n"))
                bytes.Enqueue(c);
        }

        public int Read(byte[] b, int o, int n)
        {
            Thread.Sleep(5);
            if (bytes.Count == 0)
                throw new TimeoutException();
            b[o] = bytes.Dequeue();
            return 1;
        }

        public void Dispose()
        {
        }
    }

    [Fact]
    public async Task ResponseDeadlineCoversTheWholeLine()
    {
        await using var usb = new UsbMonoPodTransport(() => new Trickle(), responseTimeout: TimeSpan.FromMilliseconds(20));
        var watch = System.Diagnostics.Stopwatch.StartNew();
        await Assert.ThrowsAsync<TimeoutException>(() => usb.ConnectAsync(default));
        Assert.True(watch.Elapsed < TimeSpan.FromSeconds(2));
    }
}
