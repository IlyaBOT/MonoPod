namespace MonoPod.Core;
public sealed partial class WiFiMonoPodTransport : ILibraryTransport
{
    public async Task<LibraryInfo> LibraryInfoAsync(CancellationToken ct)=>WireJson.Read<LibraryInfo>(await Get("api/library/info",ct));
    public Task RefreshLibraryAsync(CancellationToken ct)=>Post("api/library/refresh",ct);
    public async Task<LibraryBlock> ReadLibraryAsync(long offset,int count,CancellationToken ct)
    {
        if(offset<0 || count<1 || count>16384)throw new ArgumentOutOfRangeException(nameof(count));
        using var ownership=EnterNormalRequest();using var deadline=Deadline(ct);
        using var response=await client.GetAsync($"api/library/db?offset={offset}&count={count}",System.Net.Http.HttpCompletionOption.ResponseHeadersRead,deadline.Token);
        if(!response.IsSuccessStatusCode)throw new DeviceException(await response.Content.ReadAsStringAsync(deadline.Token));
        await using var stream=await response.Content.ReadAsStreamAsync(deadline.Token);var data=new byte[count];
        await stream.ReadExactlyAsync(data,deadline.Token);
        if(await stream.ReadAsync(new byte[1],deadline.Token)!=0 || !response.Headers.TryGetValues("X-CRC32",out var crc) || LibrarySnapshot.Hex(crc.Single())!=Crc32.Compute(data) ||
            !response.Headers.TryGetValues("X-DB-Generation",out var gen) || !uint.TryParse(gen.Single(),out var generation))throw new InvalidDataException("DB block CRC/generation/length mismatch.");
        return new(data,generation);
    }
}
