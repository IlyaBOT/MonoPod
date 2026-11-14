using MonoPod.Core;
using Xunit;
namespace MonoPod.Desktop.Tests;
public sealed class MetadataEditingTests
{
    sealed class Legacy:HttpMessageHandler
    {
        public int Requests;
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage r,CancellationToken ct)
        {Requests++;Assert.Equal("/api/info",r.RequestUri!.AbsolutePath);return Task.FromResult(new HttpResponseMessage(System.Net.HttpStatusCode.OK){Content=new StringContent("{\"protocol\":0}")});}
    }
    [Fact]public async Task LegacyNonAtomicUploadIsRefusedBeforeAnyFileRequest()
    {
        var handler=new Legacy();await using var t=new WiFiMonoPodTransport("localhost",handler);
        await Assert.ThrowsAsync<NotSupportedException>(()=>MetadataEditing.CommitAsync(t,new("unused","/Music/test.mp3",0,0,null,"unused","unused"),null,default));Assert.Equal(1,handler.Requests);
    }
    [Theory][InlineData("mp3")][InlineData("flac")]
    public async Task TagsPreserveArtworkCustomFieldsAudioAndTrackTotal(string ext)
    {
        string folder=Path.Combine(Path.GetTempPath(),Guid.NewGuid().ToString());Directory.CreateDirectory(folder);
        string path=Path.Combine(folder,"test+."+ext);File.Copy(Path.Combine(AppContext.BaseDirectory,"Fixtures","tone."+ext),path);
        byte[] picture=Convert.FromBase64String("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aB9sAAAAASUVORK5CYII=");
        using(var f=TagLib.File.Create(path))
        {
            f.Tag.Comment="Untouched comment";f.Tag.TrackCount=12;f.Tag.Pictures=[new TagLib.Picture(picture){MimeType="image/png",Type=TagLib.PictureType.FrontCover}];
            if(ext=="mp3")TagLib.Id3v2.UserTextInformationFrame.Get((TagLib.Id3v2.Tag)f.GetTag(TagLib.TagTypes.Id3v2,true),"CUSTOM",true).Text=["KEEP"];
            else ((TagLib.Ogg.XiphComment)f.GetTag(TagLib.TagTypes.Xiph,true)).SetField("CUSTOM","KEEP");f.Save();
        }
        string AudioHash()
        {
            var start=new System.Diagnostics.ProcessStartInfo("C:\\ffmpeg\\bin\\ffmpeg.exe"){RedirectStandardOutput=true,RedirectStandardError=true,UseShellExecute=false,CreateNoWindow=true};
            foreach(string arg in new[]{"-v","error","-i",path,"-map","0:a:0","-f","hash","-hash","sha256","-"})start.ArgumentList.Add(arg);
            using var process=System.Diagnostics.Process.Start(start)!;string hash=process.StandardOutput.ReadToEnd();process.WaitForExit();Assert.Equal(0,process.ExitCode);return hash;
        }
        string before=AudioHash();await Task.Run(()=>MetadataEditing.Write(path,new("Русский + % \"","Артист","Альбом","Album Artist","2026","Rock","3")));
        Assert.Equal("Русский + % \"",MetadataEditing.Read(path).Title);
        using(var f=TagLib.File.Create(path))
        {
            Assert.Equal(12u,f.Tag.TrackCount);Assert.Equal("Untouched comment",f.Tag.Comment);Assert.Equal(picture,f.Tag.Pictures.Single().Data.Data);
            if(ext=="mp3")Assert.Equal("KEEP",TagLib.Id3v2.UserTextInformationFrame.Get((TagLib.Id3v2.Tag)f.GetTag(TagLib.TagTypes.Id3v2),"CUSTOM",false).Text.Single());
            else Assert.Equal("KEEP",((TagLib.Ogg.XiphComment)f.GetTag(TagLib.TagTypes.Xiph)).GetField("CUSTOM").Single());
        }
        Assert.Equal(before,AudioHash());Directory.Delete(folder,true);
    }
    [Fact]public void UnsupportedFormatsAreReadOnly()=>Assert.Throws<NotSupportedException>(()=>MetadataEditing.Write("test.aac",new("","","","","","","")));
    [Fact]public async Task AtomicReplaceChecksSpaceAndRetainsPcFilesOnDisconnect()
    {
        string folder=Path.Combine(Path.GetTempPath(),Guid.NewGuid().ToString());
        byte[] original=await File.ReadAllBytesAsync(Path.Combine(AppContext.BaseDirectory,"Fixtures","tone.mp3"));
        var link=new UsbDevice();link.Files["/Music/test.mp3"]=original;
        await using var t=new UsbMonoPodTransport(()=>link);await t.ConnectAsync(default);await t.StartSyncAsync(default);
        var work=await MetadataEditing.PrepareAsync(t,await t.StatAsync("/Music/test.mp3",default),null,folder,null,null,default);
        work=await MetadataEditing.EditAsync(work,new("Changed","Artist","Album","","2026","","1"),default);
        link.FreeBytes=0;await Assert.ThrowsAsync<DeviceException>(()=>MetadataEditing.CommitAsync(t,work,null,default));
        Assert.DoesNotContain(link.Commands,c=>c.StartsWith("MPOD1 X PUT "));Assert.Equal(original,link.Files["/Music/test.mp3"]);
        link.FreeBytes=30000000000;link.DisconnectAt=100;
        await Assert.ThrowsAsync<IOException>(()=>MetadataEditing.CommitAsync(t,work,null,default));
        Assert.Equal(original,link.Files["/Music/test.mp3"]);Assert.True(File.Exists(work.EditedFile));Assert.Equal(original,await File.ReadAllBytesAsync(work.OriginalFile));
        var retry=new UsbDevice();retry.Files["/Music/test.mp3"]=original;
        await using var other=new UsbMonoPodTransport(()=>retry);await other.ConnectAsync(default);await other.StartSyncAsync(default);
        await MetadataEditing.CommitAsync(other,work,null,default);
        Assert.Equal(work.EditedCrc,Crc32.Compute(retry.Files["/Music/test.mp3"]));Assert.True(File.Exists(Path.Combine(work.Folder,"committed.txt")));
        Assert.All(link.Commands.Where(c=>c.StartsWith("MPOD1 X READ ")),c=>Assert.InRange(int.Parse(c.Split(' ')[5]),1,1024));
        Directory.Delete(folder,true);
    }
}
