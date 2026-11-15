using System.Diagnostics;
using System.Text.Json;

namespace MonoPod.Core;
public sealed record EditableTags(string Title,string Artist,string Album,string AlbumArtist,string Year,string Genre,string TrackNumber);
public sealed record MetadataWork(string Folder,string RemotePath,long OriginalSize,long OriginalModified,uint? Generation,
    string OriginalFile,string EditedFile,uint EditedCrc=0);
public static class MetadataEditing
{
    public static bool Supported(string path)=>Path.GetExtension(path).ToLowerInvariant() is ".mp3" or ".flac";
    static TagLib.Tag Tag(TagLib.File file,string path)=>file.GetTag(Path.GetExtension(path).Equals(".mp3",StringComparison.OrdinalIgnoreCase)?TagLib.TagTypes.Id3v2:TagLib.TagTypes.Xiph,true);
    public static EditableTags Read(string path)
    {
        if(!Supported(path))throw new NotSupportedException("Writing tags is available for MP3/FLAC only; this format is read-only.");
        using var file=TagLib.File.Create(path);var t=Tag(file,path);
        return new(t.Title??"",string.Join("; ",t.Performers),t.Album??"",string.Join("; ",t.AlbumArtists),t.Year==0?"":t.Year.ToString(),string.Join("; ",t.Genres),t.Track==0?"":t.Track.ToString());
    }
    public static void Write(string path,EditableTags tags)
    {
        var before=Read(path);
        static uint Number(string value)=>string.IsNullOrWhiteSpace(value)?0:uint.TryParse(value,out uint n)&&n<=65535?n:throw new ArgumentException("Year and Track Number must be 0–65535 or empty.");
        uint year=Number(tags.Year),track=Number(tags.TrackNumber);
        using var file=TagLib.File.Create(path);var t=Tag(file,path);
        if(before.Title!=tags.Title)t.Title=tags.Title;
        if(before.Artist!=tags.Artist)t.Performers=tags.Artist.Split(';',StringSplitOptions.TrimEntries|StringSplitOptions.RemoveEmptyEntries);
        if(before.Album!=tags.Album)t.Album=tags.Album;
        if(before.AlbumArtist!=tags.AlbumArtist)t.AlbumArtists=tags.AlbumArtist.Split(';',StringSplitOptions.TrimEntries|StringSplitOptions.RemoveEmptyEntries);
        if(before.Year!=tags.Year)t.Year=year;
        if(before.Genre!=tags.Genre)t.Genres=tags.Genre.Split(';',StringSplitOptions.TrimEntries|StringSplitOptions.RemoveEmptyEntries);
        if(before.TrackNumber!=tags.TrackNumber)t.Track=track;
        file.Save();
    }
    public static async Task<MetadataWork> PrepareAsync(IMonoPodTransport transport,RemoteFileEntry entry,uint? generation,
        string recoveryRoot,string? verifiedLocal,IProgress<TransferProgress>? progress,CancellationToken ct)
    {
        if(!Supported(entry.Path))throw new NotSupportedException("Writing tags is available for MP3/FLAC only.");
        string folder=Path.Combine(recoveryRoot,Guid.NewGuid().ToString("N"));Directory.CreateDirectory(folder);
        string source=Path.Combine(folder,"original"+Path.GetExtension(entry.Path));string edited=Path.Combine(folder,"edited"+Path.GetExtension(entry.Path));
        var work=new MetadataWork(folder,entry.Path,entry.Size,entry.Modified,generation,source,edited);
        await File.WriteAllTextAsync(Path.Combine(folder,"operation.json"),JsonSerializer.Serialize(work),ct);
        if(verifiedLocal!=null)File.Copy(verifiedLocal,source);
        else
        {
            await using var output=new FileStream(source,FileMode.CreateNew,FileAccess.Write,FileShare.Read,16384,true);
            long offset=0;var watch=Stopwatch.StartNew();
            while(offset<entry.Size)
            {
                int wanted=(int)Math.Min(transport.IsUsb?1024:16384,entry.Size-offset);
                var bytes=await transport.ReadRangeAsync(entry.Path,offset,wanted,ct);
                if(bytes.Length!=wanted)throw new EndOfStreamException("Incomplete metadata download; partial PC file retained: "+folder);
                await output.WriteAsync(bytes,ct);offset+=bytes.Length;progress?.Report(new(entry.Name,offset,entry.Size,watch.Elapsed));
            }
            await output.FlushAsync(ct);output.Flush(true);
        }
        var after=await transport.StatAsync(entry.Path,ct);
        if(after.Size!=entry.Size || after.Modified!=entry.Modified)throw new IOException("Remote file changed during download; recovery copy retained: "+folder);
        return work;
    }
    public static async Task<MetadataWork> EditAsync(MetadataWork work,EditableTags tags,CancellationToken ct)
    {
        await Task.Run(()=>{ct.ThrowIfCancellationRequested();File.Copy(work.OriginalFile,work.EditedFile,true);Write(work.EditedFile,tags);},ct);
        work=work with{EditedCrc=await Crc32.FileAsync(work.EditedFile,ct)};
        await File.WriteAllTextAsync(Path.Combine(work.Folder,"operation.json"),JsonSerializer.Serialize(work),ct);return work;
    }
    public static async Task CommitAsync(IMonoPodTransport transport,MetadataWork work,IProgress<TransferProgress>? progress,CancellationToken ct)
    {
        var info=await transport.GetDeviceInfoAsync(ct);
        if(info.Protocol<4 || !transport.IsUsb && !info.Supports("fs_upload"))throw new NotSupportedException("Metadata replacement requires MPOD1 v4 atomic file uploads.");
        long size=new FileInfo(work.EditedFile).Length;
        if(info.FreeBytes<size+65536)throw new DeviceException("NO_SPACE Atomic replacement requires space for another full file plus index overhead.");
        var stat=await transport.StatAsync(work.RemotePath,ct);
        if(stat.Size!=work.OriginalSize || stat.Modified!=work.OriginalModified)throw new IOException("Remote original changed; replacement refused.");
        if(work.Generation is {} gen && transport is ILibraryTransport db)
        {var index=await db.LibraryInfoAsync(ct);if(index.Dirty || index.Generation!=gen)throw new IOException("Library changed since editing began; reload the track before retrying.");}
        if(await Crc32.FileAsync(work.EditedFile,ct)!=work.EditedCrc)throw new InvalidDataException("Prepared local file changed; replacement refused.");
        await transport.UploadAsync(work.EditedFile,work.RemotePath,true,progress,ct);
        await File.WriteAllTextAsync(Path.Combine(work.Folder,"committed.txt"),DateTimeOffset.UtcNow.ToString("O"),CancellationToken.None);
        // Retain both PC copies even after success; only the user removes recovery files.
    }
}
