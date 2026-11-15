using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using MonoPod.Core;
using Xunit;

namespace MonoPod.Desktop.Tests;

public sealed class OtaNetworkTests
{
    sealed class LocalDevice : IAsyncDisposable
    {
        readonly HttpListener server = new();
        readonly CancellationTokenSource stop = new();
        readonly Task loop;
        public string Url { get; }
        public byte[] Body = [];
        public long DeclaredLength;
        public string Sha = "", Size = "", Pin = "", ContentType = "";
        public bool Chunked;
        public bool DropAck;
        public LocalDevice()
        {
            var socket = new TcpListener(IPAddress.Loopback,0); socket.Start();
            int port = ((IPEndPoint)socket.LocalEndpoint).Port; socket.Stop();
            Url = $"http://127.0.0.1:{port}/"; server.Prefixes.Add(Url); server.Start(); loop = Loop();
        }
        async Task Loop()
        {
            try
            {
                while(!stop.IsCancellationRequested)
                {
                    var context = await server.GetContextAsync().WaitAsync(stop.Token);
                    try
                    {
                        object response;
                        switch(context.Request.Url!.AbsolutePath)
                        {
                            case "/api/ota/info": response = FirmwareFixture.Target; break;
                            case "/api/info": response = new DeviceInfo { Firmware = "0.10.6.6", Protocol = 4, SyncMode = "wifi" }; break;
                            case "/api/ota/upload":
                                DeclaredLength=context.Request.ContentLength64;
                                ContentType=context.Request.ContentType??"";
                                Chunked=context.Request.Headers["Transfer-Encoding"]?.Contains("chunked")==true;
                                Sha=context.Request.Headers["X-MonoPod-OTA-SHA256"]??"";
                                Size=context.Request.Headers["X-MonoPod-OTA-Size"]??"";
                                Pin=context.Request.Headers["X-MonoPod-OTA-PIN"]??"";
                                using(var body=new MemoryStream()){await context.Request.InputStream.CopyToAsync(body,stop.Token);Body=body.ToArray();}
                                if(DropAck){context.Response.Abort();continue;}
                                response=new {ok=true,status="verified",rebooting=true};break;
                            default:context.Response.StatusCode=404;response=new {ok=false};break;
                        }
                        byte[] json=JsonSerializer.SerializeToUtf8Bytes(response,WireJson.Options);
                        context.Response.ContentType="application/json";context.Response.ContentLength64=json.Length;
                        await context.Response.OutputStream.WriteAsync(json,stop.Token);context.Response.Close();
                    }
                    catch(Exception){context.Response.Abort();}
                }
            }
            catch(Exception error) when(error is OperationCanceledException or HttpListenerException or ObjectDisposedException){ }
        }
        public async ValueTask DisposeAsync(){stop.Cancel();server.Close();await loop;stop.Dispose();}
    }

    [Fact]
    public async Task RealLocalHttpConnectionHasContentLengthAndExactFirmwareBytes()
    {
        using var image=new FirmwareFixture();await using var device=new LocalDevice();
        await using var wifi=new WiFiMonoPodTransport(device.Url);
        var target=await wifi.GetOtaInfoAsync(default);
        var receipt=await wifi.UploadFirmwareAsync(image.Path,target,"654321",null,null,default);
        Assert.Equal(device.Body.LongLength,device.DeclaredLength);Assert.False(device.Chunked);
        Assert.StartsWith("multipart/form-data; boundary=",device.ContentType);
        string wire=Encoding.Latin1.GetString(device.Body);
        int begin=wire.IndexOf("\r\n\r\n",StringComparison.Ordinal)+4;
        int end=wire.LastIndexOf("\r\n--",StringComparison.Ordinal);
        Assert.Equal(image.Bytes,device.Body[begin..end]);
        Assert.Equal(receipt.Sha256,device.Sha);Assert.Equal(image.Bytes.Length.ToString(),device.Size);
        Assert.Equal("654321",device.Pin);
    }

    [Fact]
    public async Task RealConnectionDroppedAfterBodyIsNotReportedAsSuccess()
    {
        using var image=new FirmwareFixture();await using var device=new LocalDevice { DropAck=true };
        await using var wifi=new WiFiMonoPodTransport(device.Url) { OtaAcknowledgementTimeout=TimeSpan.FromSeconds(2) };
        var error=await Assert.ThrowsAsync<OtaTransferException>(()=>wifi.UploadFirmwareAsync(image.Path,FirmwareFixture.Target,"654321",null,null,default));
        Assert.True(error.OutcomeUncertain);Assert.Equal(image.Bytes.Length,error.BytesSent);
        Assert.Contains("unknown",error.Message);
    }
}
