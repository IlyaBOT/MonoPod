namespace MonoPod.Core;
public sealed partial class UsbMonoPodTransport : ILibraryTransport
{
    void NeedDb(){if(!sync)throw new DeviceException("SYNC_REQUIRED");if(!info.Supports("library_db_v1"))throw new NotSupportedException("library_db_v1 unavailable.");}
    public Task<LibraryInfo> LibraryInfoAsync(CancellationToken ct)=>Run(c=>{NeedDb();return WireJson.Read<LibraryInfo>(Json(X("DBINFO",c)));},ct);
    public Task RefreshLibraryAsync(CancellationToken ct)=>Run(c=>{NeedDb();var reply=X("DBREFRESH",c);if(!reply.StartsWith("OK GENERATION ") || !uint.TryParse(reply[14..],out _))throw new InvalidDataException("Invalid DBREFRESH acknowledgement.");return true;},ct);
    public Task<LibraryBlock> ReadLibraryAsync(long offset,int count,CancellationToken ct)=>Run(c=>
    {
        NeedDb();if(offset<0 || count<1 || count>1024)throw new ArgumentOutOfRangeException(nameof(count));
        var fields=X($"DBREAD {offset} {count}",c).Split(' ');
        if(fields.Length!=3 || fields[0]!="DATA" || !int.TryParse(fields[1],out int size) || size!=count)throw new InvalidDataException("Invalid DBREAD binary header.");
        var crc=LibrarySnapshot.Hex(fields[2]);var data=Read(size,c);if(Crc32.Compute(data)!=crc)throw new LibraryBlockException("DBREAD CRC32 mismatch.");
        return new LibraryBlock(data,null);
    },ct);
}
