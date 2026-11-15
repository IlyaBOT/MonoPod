using MonoPod.Core;
using System.Net;
using Xunit;

namespace MonoPod.Desktop.Tests;
public sealed class TransferTimeoutTests
{
    sealed class SlowSink(int delay) : Stream
    {
        public override bool CanRead => false;
        public override bool CanSeek => false;
        public override bool CanWrite => true;
        public override long Length => 0;
        public override long Position { get => 0; set => throw new NotSupportedException(); }

        public override void Flush()
        {
        }

        public override int Read(byte[] b, int o, int n) => throw new NotSupportedException();
        public override long Seek(long n, SeekOrigin o) => throw new NotSupportedException();
        public override void SetLength(long n) => throw new NotSupportedException();
        public override void Write(byte[] b, int o, int n) => throw new NotSupportedException();
        public override async ValueTask WriteAsync(ReadOnlyMemory<byte> b, CancellationToken ct = default) => await Task.Delay(delay, ct);
        public override async Task WriteAsync(byte[] b, int o, int n, CancellationToken ct) => await Task.Delay(delay, ct);
    }

    sealed class SlowHandler(int delay) : HttpMessageHandler
    {
        protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken ct)
        {
            if (request.RequestUri!.AbsolutePath == "/api/info")
                return new(HttpStatusCode.OK)
                {
                    Content = new StringContent("{\"firmware\":\"0.10.0\",\"protocol\":4,\"sync_mode\":\"wifi\",\"capabilities\":[\"fs_upload\"]}")
                };
            using var sink = new SlowSink(delay);
            await request.Content!.CopyToAsync(sink, ct);
            return new(HttpStatusCode.OK)
            {
                Content = new StringContent("{\"ok\":true}")
            };
        }
    }

    [Fact]
    public async Task UploadMayExceedOrdinaryRequestBudgetWhileMakingProgress()
    {
        await using var wifi = new WiFiMonoPodTransport("http://127.0.0.1", new SlowHandler(5), TimeSpan.FromMilliseconds(200), TimeSpan.FromSeconds(1));
        await wifi.ConnectAsync(default);
        string path = Path.GetTempFileName();
        using (var file = File.OpenWrite(path))
            file.SetLength(2 * 1024 * 1024);
        try
        {
            var watch = System.Diagnostics.Stopwatch.StartNew();
            await wifi.UploadAsync(path, "/Music/long.mp3", false, null, default);
            Assert.True(watch.Elapsed > TimeSpan.FromMilliseconds(200));
        }
        finally
        {
            File.Delete(path);
        }
    }

    [Fact]
    public async Task UploadWithNoProgressHitsItsInactivityDeadline()
    {
        await using var wifi = new WiFiMonoPodTransport("http://127.0.0.1", new SlowHandler(1000), TimeSpan.FromSeconds(2), TimeSpan.FromMilliseconds(50));
        await wifi.ConnectAsync(default);
        string path = Path.GetTempFileName();
        File.WriteAllBytes(path, new byte[10000]);
        try
        {
            await Assert.ThrowsAnyAsync<OperationCanceledException>(() => wifi.UploadAsync(path, "/Music/stalled.mp3", false, null, default));
        }
        finally
        {
            File.Delete(path);
        }
    }
}
