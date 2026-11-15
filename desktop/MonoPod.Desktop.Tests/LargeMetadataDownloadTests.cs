using MonoPod.Core;
using Xunit;
namespace MonoPod.Desktop.Tests;
[Collection("WinForms theme regressions")]
public sealed class LargeMetadataDownloadTests
{
    // Synthetic 263 MiB transfer. Real codec/tag preservation is checked separately.
    sealed class Source:IMonoPodTransport
    {
        public const long Length=263L*1024*1024;public string Description=>"synthetic";public bool IsUsb=>false;
        public int Largest;public long Peak;
        public Task<RemoteFileEntry> StatAsync(string p,CancellationToken c)=>Task.FromResult(new RemoteFileEntry{Path=p,Name="large.flac",Size=Length});
        public Task<byte[]> ReadRangeAsync(string p,long o,int n,CancellationToken c)
        {Largest=Math.Max(Largest,n);Peak=Math.Max(Peak,GC.GetTotalMemory(false));return Task.FromResult(new byte[n]);}
        public ValueTask DisposeAsync()=>ValueTask.CompletedTask;
        public Task<DeviceInfo> ConnectAsync(CancellationToken c)=>throw new NotSupportedException();
        public Task<DeviceInfo> GetDeviceInfoAsync(CancellationToken c)=>throw new NotSupportedException();
        public Task StartSyncAsync(CancellationToken c)=>throw new NotSupportedException();public Task EndSyncAsync(CancellationToken c)=>throw new NotSupportedException();
        public Task<IReadOnlyList<RemoteFileEntry>> ListAsync(string p,CancellationToken c)=>throw new NotSupportedException();
        public Task UploadAsync(string a,string b,bool r,IProgress<TransferProgress>? p,CancellationToken c)=>throw new NotSupportedException();
        public Task DownloadAsync(string a,string b,IProgress<TransferProgress>? p,CancellationToken c)=>throw new NotSupportedException();
        public Task DeleteAsync(string p,bool d,CancellationToken c)=>throw new NotSupportedException();public Task CreateDirectoryAsync(string p,CancellationToken c)=>throw new NotSupportedException();
        public Task RenameAsync(string a,string b,CancellationToken c)=>throw new NotSupportedException();public Task UploadThemeAsync(byte[] d,CancellationToken c)=>throw new NotSupportedException();
        public Task ScanAsync(CancellationToken c)=>throw new NotSupportedException();public Task SetSmoothingAsync(bool e,CancellationToken c)=>throw new NotSupportedException();
    }
    [Fact]public async Task LargeDownloadUsesBoundedChunksAndWorkingMemory()
    {
        string root=Path.Combine(Path.GetTempPath(),Guid.NewGuid().ToString());var source=new Source();long baseline=GC.GetTotalMemory(true);
        try
        {
            var work=await MetadataEditing.PrepareAsync(source,await source.StatAsync("/Music/large.flac",default),null,root,null,null,default);
            Assert.Equal(Source.Length,new FileInfo(work.OriginalFile).Length);Assert.Equal(16384,source.Largest);
            Assert.True(source.Peak-baseline<64L*1024*1024,$"Working managed memory grew by {source.Peak-baseline} bytes");
        }
        finally{if(Directory.Exists(root))Directory.Delete(root,true);}
    }
}
