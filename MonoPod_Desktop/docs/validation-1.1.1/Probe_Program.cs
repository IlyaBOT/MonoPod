using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.Json;
using MonoPod.Core;

if (args.Length < 2 || args[0] is not ("--info" or "--test"))
{
    Console.WriteLine("UsbProbe --info COM18 | --test COM18 <local-work-dir>");
    return 2;
}
using var deadline = new CancellationTokenSource(TimeSpan.FromMinutes(3));
var ct = deadline.Token;
await using var usb = new UsbMonoPodTransport(args[1]);
var info = await usb.ConnectAsync(ct);
Console.WriteLine("INFO " + JsonSerializer.Serialize(info, WireJson.Options));
if (args[0] == "--info") return 0;
if (args.Length != 3 || info.Protocol != 4 || !info.SdReady) throw new InvalidOperationException("Expected --test COM <folder>, v4 and mounted SD.");
string dir = Path.GetFullPath(args[2]);
Directory.CreateDirectory(dir);
const string remote = "/Desktop_USB_test_20261031.bin";
string upload = Path.Combine(dir,"probe-upload.bin"), download = Path.Combine(dir,"probe-download.bin");
bool attempted = false, saved = false, deleted = false;
RemoteFileEntry? before = null, after = null;
var timer = Stopwatch.StartNew();
try
{
    await usb.StartSyncAsync(ct);
    try
    {
        await usb.StatAsync(remote,ct);
        throw new InvalidOperationException("Test target already exists; refusing to overwrite it.");
    }
    catch(DeviceException e) when(e.Code=="NOT_FOUND") { }
    before=await usb.StatAsync("/MonoPod/badapple.bav",ct);
    byte[] payload=Enumerable.Range(0,32768).Select(i=>(byte)(i*37+11)).ToArray();
    await File.WriteAllBytesAsync(upload,payload,ct);
    Console.WriteLine($"TEST {remote} {payload.Length} bytes CRC={Crc32.Compute(payload):X8}; replace=false");
    attempted=true;
    await usb.UploadAsync(upload,remote,false,new Progress<TransferProgress>(p=>Console.WriteLine($"SENT {p.Completed}/{p.Total}")),ct);
    saved=true;
    await usb.DownloadAsync(remote,download,null,ct);
    byte[] actual=await File.ReadAllBytesAsync(download,ct);
    if(!payload.SequenceEqual(actual))throw new InvalidDataException("Readback mismatch");
    Console.WriteLine("VERIFIED SHA256="+Convert.ToHexString(SHA256.HashData(actual)));
    await usb.DeleteAsync(remote,false,ct);deleted=true;
    after=await usb.StatAsync("/MonoPod/badapple.bav",ct);
    if(before.Size!=after.Size||before.Modified!=after.Modified)throw new InvalidDataException("Existing BAV stat changed");
    await usb.EndSyncAsync(ct);
    var ended=await usb.GetDeviceInfoAsync(ct);
    if(ended.SyncMode!="none")throw new InvalidDataException("Device is still in Sync");
    var result=new{passed=true,remote,bytes=payload.Length,crc=Crc32.Compute(payload).ToString("X8"),sha256=Convert.ToHexString(SHA256.HashData(actual)),deleted,elapsed_seconds=timer.Elapsed.TotalSeconds,bav_stat_unchanged=true,sync_mode=ended.SyncMode,firmware=info.Firmware};
    await File.WriteAllTextAsync(Path.Combine(dir,"physical-result.json"),JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true}),ct);
    Console.WriteLine("PASS "+JsonSerializer.Serialize(result));
    return 0;
}
catch(Exception e)
{
    Console.WriteLine("FAIL "+e);
    if(usb.NeedsRecovery)
    {
        using var cleanup=new CancellationTokenSource(TimeSpan.FromSeconds(90));
        try{await usb.RecoverAndEndSyncAsync(cleanup.Token,new Progress<string>(Console.WriteLine));Console.WriteLine("RECOVERY Sync ended");}
        catch(Exception recovery){Console.WriteLine("RECOVERY FAILED "+recovery.Message);}
    }
    else
    {
        using var cleanup=new CancellationTokenSource(TimeSpan.FromSeconds(60));
        if(saved&&!deleted){try{await usb.DeleteAsync(remote,false,cleanup.Token);Console.WriteLine("Deleted own test file");}catch(Exception cleanupError){Console.WriteLine("Own test file remains: "+cleanupError.Message);}}
        try{await usb.EndSyncAsync(cleanup.Token);}catch(Exception cleanupError){Console.WriteLine("End Sync failed: "+cleanupError.Message);}
    }
    if(attempted&&!deleted)Console.WriteLine("Inspect only the test path if needed: "+remote);
    return 1;
}
