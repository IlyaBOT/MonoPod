using System.Buffers.Binary;
using System.Net;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using MonoPod.Core;

namespace MonoPod.Desktop.Tests;

internal sealed class FirmwareFixture : IDisposable
{
    readonly string folder = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "MonoPod-OTA-" + Guid.NewGuid().ToString("N"));
    public string Path { get; }
    public byte[] Bytes { get; }
    public static OtaInfo Target => new() { Protocol = 1, Firmware = "0.10.6.6", Board = "esp32s3", DeviceId = "MP-TEST-S3", Supported = true, MaxImageBytes = 7340032, PinRequired = true, PinLocation = "device_screen" };
    public FirmwareFixture(int payload = 65536, int chip = 9, bool hash = true, string name = "application.bin", int flashMode = 2)
    {
        Directory.CreateDirectory(folder);
        Path = System.IO.Path.Combine(folder, name);
        using var output = new MemoryStream();
        var header = new byte[24]; header[0] = 0xE9; header[1] = 1; header[2] = (byte)flashMode; header[23] = hash ? (byte)1 : (byte)0;
        BinaryPrimitives.WriteUInt16LittleEndian(header.AsSpan(12), (ushort)chip);
        output.Write(header);
        var segment = new byte[8];
        BinaryPrimitives.WriteUInt32LittleEndian(segment, 0x3C000020);
        BinaryPrimitives.WriteUInt32LittleEndian(segment.AsSpan(4), (uint)payload);
        output.Write(segment);
        var data = new byte[payload];
        for (int i = 256; i < payload; i++) data[i] = (byte)(i * 17);
        BinaryPrimitives.WriteUInt32LittleEndian(data, 0xABCD5432);
        Encoding.ASCII.GetBytes("0.10.7-test").CopyTo(data, 16);
        Encoding.ASCII.GetBytes("DIY_iPod_Mono").CopyTo(data, 48);
        output.Write(data);
        byte checksum = 0xEF; foreach (byte b in data) checksum ^= b;
        while (output.Length % 16 != 15) output.WriteByte(0);
        output.WriteByte(checksum);
        if (hash) output.Write(SHA256.HashData(output.ToArray()));
        Bytes = output.ToArray(); File.WriteAllBytes(Path, Bytes);
    }
    public void Dispose() { foreach (var file in Directory.EnumerateFiles(folder)) File.Delete(file); Directory.Delete(folder); }
}

internal sealed class InlineProgress<T>(Action<T> action) : IProgress<T>
{
    public void Report(T value) => action(value);
}

internal sealed class OtaHttpHandler : HttpMessageHandler
{
    public OtaInfo Info { get; set; } = FirmwareFixture.Target;
    public DeviceInfo Device { get; set; } = new() { Firmware = "0.10.6.6", Protocol = 4, SdReady = true, SyncMode = "wifi", Capabilities = ["list", "clock_sync", "ntp", "ntp_server", "sync_end"] };
    public HttpStatusCode InfoStatus { get; set; } = HttpStatusCode.OK;
    public HttpStatusCode UploadStatus { get; set; } = HttpStatusCode.OK;
    public string Acknowledgement { get; set; } = "{\"ok\":true,\"status\":\"verified\",\"rebooting\":true}";
    public Func<HttpRequestMessage,CancellationToken,Task>? UploadBody { get; set; }
    public Func<HttpRequestMessage,CancellationToken,Task>? NormalRequest { get; set; }
    public byte[] Body { get; private set; } = [];
    public string PinHeader { get; private set; } = "";
    public string ShaHeader { get; private set; } = "";
    public string SizeHeader { get; private set; } = "";
    public long? ContentLength { get; private set; }
    public string ContentType { get; private set; } = "";
    public List<string> Routes { get; } = [];
    public string ClockBody { get; private set; } = "";
    public int Uploads { get; private set; }
    public static HttpResponseMessage Json(object value, HttpStatusCode status = HttpStatusCode.OK) =>
        new(status) { Content = new StringContent(JsonSerializer.Serialize(value, WireJson.Options)) };
    protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken ct)
    {
        string route = request.RequestUri!.AbsolutePath; Routes.Add(route);
        if (route == "/api/ota/info") return Json(Info, InfoStatus);
        if (route == "/api/info") return Json(Device);
        if (route == "/api/clock")
        {
            ClockBody = await request.Content!.ReadAsStringAsync(ct);
            return Json(new { ok = true });
        }
        if (route == "/api/ota/upload")
        {
            Uploads++;
            PinHeader = request.Headers.GetValues("X-MonoPod-OTA-PIN").Single();
            ShaHeader = request.Headers.GetValues("X-MonoPod-OTA-SHA256").Single();
            SizeHeader = request.Headers.GetValues("X-MonoPod-OTA-Size").Single();
            ContentLength = request.Content!.Headers.ContentLength;
            ContentType = request.Content.Headers.ContentType!.ToString();
            if (UploadBody != null) await UploadBody(request, ct);
            else { using var body = new MemoryStream(); await request.Content.CopyToAsync(body, ct); Body = body.ToArray(); }
            return new(UploadStatus) { Content = new StringContent(Acknowledgement) };
        }
        if (NormalRequest != null) await NormalRequest(request, ct);
        return Json(new DirectoryPage());
    }
}

internal sealed class OtaSlowSink(int delay) : Stream
{
    public override bool CanRead => false;
    public override bool CanSeek => false;
    public override bool CanWrite => true;
    public override long Length => 0;
    public override long Position { get => 0; set => throw new NotSupportedException(); }
    public override void Flush() { }
    public override int Read(byte[] bytes, int offset, int count) => throw new NotSupportedException();
    public override long Seek(long offset, SeekOrigin origin) => throw new NotSupportedException();
    public override void SetLength(long value) => throw new NotSupportedException();
    public override void Write(byte[] bytes, int offset, int count) => throw new NotSupportedException();
    public override async Task WriteAsync(byte[] bytes, int offset, int count, CancellationToken ct) => await Task.Delay(delay, ct);
    public override async ValueTask WriteAsync(ReadOnlyMemory<byte> bytes, CancellationToken ct = default) => await Task.Delay(delay, ct);
}
