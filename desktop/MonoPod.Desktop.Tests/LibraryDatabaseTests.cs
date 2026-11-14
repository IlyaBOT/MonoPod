using System.Net;
using System.Text;
using System.Text.Json;
using MonoPod.Core;
using Xunit;
namespace MonoPod.Desktop.Tests;
public sealed class LibraryDatabaseTests
{
    public static byte[] Database(int count=54,uint generation=7)
    {
        string rows=string.Concat(Enumerable.Range(0,count).Select(i=>$"T\t/Music/Тест+%25 {i}.flac\t1000\t0\t01234567\t00000000\t123000\t2026\t1\t44100\t900\tFLAC\t.flac\tРусский+%25%09\"\tАртист\tАльбом\tArtist\tRock\t123\t44100\n"));
        return Encoding.UTF8.GetBytes($"MPODDB\t1\t{generation}\t{count}\t0\n"+rows+$"END\t{count}\t{Crc32.Compute(Encoding.UTF8.GetBytes(rows)):X8}\n");
    }
    [Fact]public void ParserChecksSchemaCountFooterCrcAndUnicode()
    {
        var bytes=Database();var db=LibrarySnapshot.Parse(bytes);Assert.Equal(54,db.Tracks.Count);
        Assert.Equal("Русский+%\t\"",db.Tracks[0].Title);Assert.Contains("Тест+% ",db.Tracks[0].Path);Assert.Equal(0u,db.Tracks[0].FullCrc);
        bytes[150]^=1;Assert.Throws<InvalidDataException>(()=>LibrarySnapshot.Parse(bytes));
        foreach(var s in new[]{Encoding.UTF8.GetString(Database()).Replace("MPODDB\t1","MPODDB\t2"),Encoding.UTF8.GetString(Database()).Replace("%25","%ZZ"),Encoding.UTF8.GetString(Database()).Replace("END\t54","END\t53"),Encoding.UTF8.GetString(Database()).Replace("1.flac","0.flac")})Assert.ThrowsAny<Exception>(()=>LibrarySnapshot.Parse(Encoding.UTF8.GetBytes(s)));
        Assert.Throws<InvalidDataException>(()=>LibrarySnapshot.Parse(Database()[..^12]));
    }
    sealed class Handler:HttpMessageHandler
    {
        public int Blocks,Refreshes,OtherReads;public bool Dirty,BadCrcOnce,ChangeGenerationOnce,AlwaysCorrupt,TruncateOnce;public uint Gen=7;
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request,CancellationToken ct)
        {
            var url=request.RequestUri!;var result=new HttpResponseMessage(HttpStatusCode.OK);byte[] db=Database(generation:Gen);
            if(url.AbsolutePath=="/api/info")result.Content=new StringContent(JsonSerializer.Serialize(new DeviceInfo{DeviceGuid="test-device",Protocol=4,SyncMode="wifi",SdReady=true,Capabilities=["library_db_v1"]},WireJson.Options));
            else if(url.AbsolutePath=="/api/library/info")result.Content=new StringContent(JsonSerializer.Serialize(new LibraryInfo(1,Gen,54,db.Length,Dirty),WireJson.Options));
            else if(url.AbsolutePath=="/api/library/refresh"){Refreshes++;Dirty=false;result.Content=new StringContent("{\"ok\":true}");}
            else if(url.AbsolutePath=="/api/library/db")
            {
                Blocks++;var query=url.Query.TrimStart('?').Split('&').Select(s=>s.Split('=')).ToDictionary(s=>s[0],s=>int.Parse(s[1]));Assert.InRange(query["count"],1,16384);
                var data=db.AsSpan(query["offset"],query["count"]).ToArray();uint crc=Crc32.Compute(data);
                if(BadCrcOnce || AlwaysCorrupt){crc^=1;BadCrcOnce=false;}
                if(TruncateOnce){data=data[..^1];TruncateOnce=false;}
                result.Content=new ByteArrayContent(data);result.Headers.Add("X-CRC32",crc.ToString("X8"));
                result.Headers.Add("X-DB-Generation",(ChangeGenerationOnce?Gen+1:Gen).ToString());ChangeGenerationOnce=false;
            }
            else{OtherReads++;throw new IOException("Unexpected per-file request: "+url);}
            return Task.FromResult(result);
        }
    }
    [Fact]public async Task CacheReconnectIdentityDirtyAndBatchRefresh()
    {
        string folder=Path.Combine(Path.GetTempPath(),Guid.NewGuid().ToString());var h=new Handler();await using var t=new WiFiMonoPodTransport("localhost",h);await t.ConnectAsync(default);
        var index=new LibraryIndex(folder);await index.LoadAsync(t,"guid1",false,null,default);int blocks=h.Blocks;Assert.InRange(blocks,1,3);
        await new LibraryIndex(folder).LoadAsync(t,"guid1",false,null,default);Assert.Equal(blocks,h.Blocks);
        await new LibraryIndex(folder).LoadAsync(t,"other-device",false,null,default);Assert.True(h.Blocks>blocks);
        h.Dirty=true;await index.LoadAsync(t,"guid1",false,null,default);Assert.Equal(1,h.Refreshes);Assert.Equal(0,h.OtherReads);
        await index.LoadAsync(t,"guid1",true,null,default);Assert.Equal(2,h.Refreshes);Directory.Delete(folder,true);
    }
    [Theory][InlineData("crc")][InlineData("generation")][InlineData("truncated")]
    public async Task DamagedBlocksAreRejectedAndRetried(string mode)
    {
        var h=new Handler{BadCrcOnce=mode=="crc",ChangeGenerationOnce=mode=="generation",TruncateOnce=mode=="truncated"};await using var t=new WiFiMonoPodTransport("localhost",h);await t.ConnectAsync(default);
        await new LibraryIndex("unused").LoadAsync(t,null,false,null,default);Assert.True(h.Blocks>=2);Assert.Equal(1,h.Refreshes);
    }
    [Fact]public async Task PersistentCorruptionRetriesAreBounded()
    {
        var h=new Handler{AlwaysCorrupt=true};await using var t=new WiFiMonoPodTransport("localhost",h);await t.ConnectAsync(default);
        await Assert.ThrowsAsync<InvalidDataException>(()=>new LibraryIndex("unused").LoadAsync(t,null,false,null,default));Assert.Equal(3,h.Blocks);Assert.Equal(1,h.Refreshes);
    }
    [Fact]public async Task UsbUsesBinaryDbAndRequiresSync()
    {
        var link=new UsbDevice{Database=Database()};await using var t=new UsbMonoPodTransport(()=>link);await t.ConnectAsync(default);
        await Assert.ThrowsAsync<DeviceException>(()=>t.LibraryInfoAsync(default));Assert.DoesNotContain("MPOD1 X DBINFO",link.Commands);
        await t.StartSyncAsync(default);var index=await new LibraryIndex("unused").LoadAsync(t,null,false,null,default);Assert.Equal(54,index.Tracks.Count);
        Assert.All(link.Commands.Where(c=>c.StartsWith("MPOD1 X DBREAD ")),c=>Assert.InRange(int.Parse(c.Split(' ')[4]),1,1024));Assert.DoesNotContain(link.Commands,c=>c.StartsWith("MPOD1 X READ "));await t.RefreshLibraryAsync(default);
    }
}
