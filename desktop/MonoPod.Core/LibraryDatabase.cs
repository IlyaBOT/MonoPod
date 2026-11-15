using System.Globalization;
using System.Text;

namespace MonoPod.Core;

public sealed record LibraryInfo(int Schema, uint Generation, int Count, long Size, bool Dirty);
public sealed record LibraryBlock(byte[] Data, uint? Generation);
public sealed class LibraryBlockException(string message) : Exception(message);
public interface ILibraryTransport
{
    Task<LibraryInfo> LibraryInfoAsync(CancellationToken ct);
    Task RefreshLibraryAsync(CancellationToken ct);
    Task<LibraryBlock> ReadLibraryAsync(long offset, int count, CancellationToken ct);
}
public sealed record LibraryTrack(string Path, long Size, long Modified, uint SampleCrc, uint FullCrc,
    uint DurationMs, ushort Year, ushort TrackNumber, uint SampleRate, uint Bitrate, string Codec,
    string Extension, string Title, string Artist, string Album, string AlbumArtist, string Genre,
    ulong FlacTotalSamples, uint FlacSampleRate)
{
    public RemoteFileEntry Entry => new() { Path=Path,Name=System.IO.Path.GetFileName(Path),Size=Size,Modified=Modified };
    public AudioMetadata Metadata => new() { Title=Title,Artist=Artist,Album=Album,AlbumArtist=AlbumArtist,
        Year=Year==0?"":Year.ToString(),TrackNumber=TrackNumber==0?"":TrackNumber.ToString(),Genre=Genre,
        Seconds=DurationMs==0?null:DurationMs/1000d,Codec=Codec,Bitrate=Bitrate==0?null:checked((int)Bitrate),
        SampleRate=SampleRate==0?null:checked((int)SampleRate),DurationSource="MonoPodDB v1" };
}
public sealed record LibrarySnapshot(uint Generation, IReadOnlyList<LibraryTrack> Tracks)
{
    static readonly UTF8Encoding Utf8=new(false,true);
    public static LibrarySnapshot Parse(byte[] bytes)
    {
        if(bytes.Length==0 || bytes.Length>64*1024*1024 || bytes[^1]!=10)throw new InvalidDataException("Incomplete MonoPodDB.");
        var lines=Utf8.GetString(bytes).Split('\n');
        var head=lines[0].Split('\t');
        if(head.Length!=5 || head[0]!="MPODDB" || head[1]!="1" || head[4]!="0")throw new InvalidDataException("Unsupported MonoPodDB schema/header.");
        uint gen=Number(head[2]);int count=checked((int)Number(head[3]));
        if(count>100000 || lines.Length!=count+3)throw new InvalidDataException("MonoPodDB count/truncation mismatch.");
        var crc=new Crc32();var tracks=new List<LibraryTrack>(count);var paths=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        for(int i=1;i<=count;i++)
        {
            crc.Append(Utf8.GetBytes(lines[i]+"\n"));var a=lines[i].Split('\t');
            if(a.Length!=20 || a[0]!="T")throw new InvalidDataException("MonoPodDB track must contain 20 columns.");
            string path=Decode(a[1]);
            if(!path.StartsWith('/') || path.Length<2 || path.Contains('\0') || !paths.Add(path))throw new InvalidDataException("Invalid/duplicate MonoPodDB path.");
            if(Number(a[9])>int.MaxValue || Number(a[10])>int.MaxValue)throw new InvalidDataException("Invalid audio rate in index.");
            tracks.Add(new(path,Number(a[2]),Number(a[3]),Hex(a[4]),Hex(a[5]),Number(a[6]),
                checked((ushort)Number(a[7])),checked((ushort)Number(a[8])),Number(a[9]),Number(a[10]),
                Decode(a[11]),Decode(a[12]),Decode(a[13]),Decode(a[14]),Decode(a[15]),Decode(a[16]),Decode(a[17]),Ulong(a[18]),Number(a[19])));
        }
        var tail=lines[count+1].Split('\t');
        if(tail.Length!=3 || tail[0]!="END" || Number(tail[1])!=count || Hex(tail[2])!=crc.Value)throw new InvalidDataException("MonoPodDB footer/count/CRC32 mismatch.");
        return new(gen,tracks);
    }
    static ulong Ulong(string s)=>s.Length>0 && s.All(char.IsAsciiDigit) && ulong.TryParse(s,NumberStyles.None,CultureInfo.InvariantCulture,out var n)?n:throw new InvalidDataException("Invalid MonoPodDB integer.");
    static uint Number(string s)=>Ulong(s)<=uint.MaxValue?(uint)Ulong(s):throw new InvalidDataException("MonoPodDB integer overflow.");
    public static uint Hex(string s)=>s.Length==8 && uint.TryParse(s,NumberStyles.AllowHexSpecifier,CultureInfo.InvariantCulture,out var n)?n:throw new InvalidDataException("Invalid CRC32.");
    static string Decode(string s)
    {
        var input=Utf8.GetBytes(s);using var output=new MemoryStream();
        for(int i=0;i<input.Length;i++)
        {
            if(input[i]!=37){output.WriteByte(input[i]);continue;}
            if(i+2>=input.Length || !byte.TryParse(Encoding.ASCII.GetString(input,i+1,2),NumberStyles.AllowHexSpecifier,CultureInfo.InvariantCulture,out var v))throw new InvalidDataException("Invalid percent encoding.");
            output.WriteByte(v);i+=2;
        }
        return Utf8.GetString(output.ToArray()); // '+' is deliberately unchanged.
    }
}

public sealed class LibraryIndex(string cacheFolder)
{
    readonly SemaphoreSlim gate=new(1,1);
    IMonoPodTransport? boundTransport;string? boundGuid;
    public LibrarySnapshot? Current { get; private set; }
    public bool NeedsRefresh { get; private set; }
    public void Invalidate(){NeedsRefresh=true;Current=null;}
    public void ClearSession(){Current=null;NeedsRefresh=false;}
    public async Task<LibrarySnapshot> LoadAsync(IMonoPodTransport transport,string? deviceGuid,bool refresh,
        IProgress<string>? progress,CancellationToken ct)
    {
        if(transport is not ILibraryTransport db)throw new NotSupportedException("No library DB transport.");
        await gate.WaitAsync(ct);
        try
        {
            if(!ReferenceEquals(boundTransport,transport) || boundGuid!=deviceGuid)
            {Current=null;NeedsRefresh=false;boundTransport=transport;boundGuid=deviceGuid;}
            bool refreshed=false;
            if(refresh){Invalidate();progress?.Report("Updating Library…");await db.RefreshLibraryAsync(ct);refreshed=true;}
            for(int attempt=0;attempt<3;attempt++)
            {
                try
                {
                    var info=await db.LibraryInfoAsync(ct);
                    if(info.Schema!=1 || info.Count<0 || info.Count>100000 || info.Size<=0 || info.Size>64*1024*1024)throw new InvalidDataException("Unsupported/invalid library information.");
                    if(info.Dirty || NeedsRefresh && !refreshed)
                    {
                        Invalidate();if(refreshed)throw new InvalidDataException("Library still dirty after refresh.");
                        progress?.Report("Updating Library…");await db.RefreshLibraryAsync(ct);refreshed=true;continue;
                    }
                    if(!refresh && !NeedsRefresh && Current?.Generation==info.Generation && Current.Tracks.Count==info.Count)return Current;
                    string? cache=string.IsNullOrWhiteSpace(deviceGuid)?null:System.IO.Path.Combine(cacheFolder,
                        Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(Encoding.UTF8.GetBytes(deviceGuid)))+"-"+info.Generation+".db");
                    if(!refresh && !NeedsRefresh && cache!=null && File.Exists(cache) && new FileInfo(cache).Length==info.Size)
                    {
                        try{var data=await File.ReadAllBytesAsync(cache,ct);var cached=LibrarySnapshot.Parse(data);
                            if(data.Length==info.Size && cached.Generation==info.Generation && cached.Tracks.Count==info.Count){Current=cached;return cached;}}
                        catch(Exception e)when(e is IOException or InvalidDataException or OverflowException or DecoderFallbackException){ }
                    }
                    using var buffer=new MemoryStream();int chunk=transport.IsUsb?1024:16384;
                    while(buffer.Length<info.Size)
                    {
                        ct.ThrowIfCancellationRequested();int wanted=(int)Math.Min(chunk,info.Size-buffer.Length);
                        var block=await db.ReadLibraryAsync(buffer.Length,wanted,ct);
                        if(block.Data.Length!=wanted || block.Generation is {} generation && generation!=info.Generation)throw new InvalidDataException("DB block generation/length mismatch.");
                        buffer.Write(block.Data);progress?.Report($"Index: {buffer.Length:N0} / {info.Size:N0} bytes");
                    }
                    var after=await db.LibraryInfoAsync(ct);
                    if(after!=info || after.Dirty)throw new InvalidDataException("Index changed during download.");
                    var raw=buffer.ToArray();var snapshot=await Task.Run(()=>LibrarySnapshot.Parse(raw),ct);
                    if(snapshot.Generation!=info.Generation || snapshot.Tracks.Count!=info.Count)throw new InvalidDataException("DB header differs from DBINFO.");
                    if(cache!=null)
                    {
                        Directory.CreateDirectory(cacheFolder);string temp=cache+"."+Guid.NewGuid().ToString("N")+".tmp";
                        try{await File.WriteAllBytesAsync(temp,raw,ct);File.Move(temp,cache,true);}finally{if(File.Exists(temp))File.Delete(temp);}
                    }
                    Current=snapshot;NeedsRefresh=false;return snapshot;
                }
                catch(Exception e)when(e is InvalidDataException or LibraryBlockException or EndOfStreamException or DecoderFallbackException or OverflowException || e is DeviceException de && de.Message.Contains("DB_STALE_REFRESH_REQUIRED",StringComparison.Ordinal))
                {
                    Invalidate();if(attempt==2)throw new InvalidDataException("Index rejected after retries; use legacy listing or Update Library.",e);
                    progress?.Report("Index changed or invalid; retrying…");
                    if(!refreshed){await db.RefreshLibraryAsync(ct);refreshed=true;}
                }
            }
            throw new InvalidDataException("Index unavailable.");
        }
        finally{gate.Release();}
    }
}
