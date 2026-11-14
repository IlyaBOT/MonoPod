using MonoPod.Core;
using MonoPod.Desktop.Services;
using System.Text.Json;

namespace MonoPod.Desktop.Forms;
public sealed partial class MainForm
{
    readonly LibraryIndex index;
    readonly Dictionary<string,LibraryTrack> indexedTracks=new(StringComparer.OrdinalIgnoreCase);
    readonly Dictionary<DataGridView,List<RemoteFileEntry>> gridSources=[];
    readonly Dictionary<DataGridView,List<RemoteFileEntry>> gridViews=[];
    readonly Dictionary<DataGridView,TextBox> gridFilters=[];
    readonly Dictionary<string,string> localCopies=new(StringComparer.OrdinalIgnoreCase);
    bool dbActive,indexUnavailable;
    string? libraryDeviceGuid;
    string indexState="Index: Not loaded";
    void MarkIndexDirty(){index.Invalidate();dbActive=false;indexState="Index: Needs refresh";ShowInfo();}
    void ConfigureLibraryUi(ToolStripMenuItem tools,Control musicBar,Control fsbar)
    {
        tools.DropDownItems.Add("Edit Metadata…",null,(_,_)=>_ = EditMetadata(tabs.SelectedIndex==1?music:files));
        tools.DropDownItems.Add("Retry prepared metadata upload…",null,(_,_)=>_ = RetryMetadata());
        Add(musicBar,"Edit Metadata…",()=>_ = EditMetadata(music),"read");
        foreach(var (grid,bar) in new[]{(files,fsbar),(music,musicBar)})
        {
            var filter=new TextBox{Width=190,PlaceholderText="Filter any field (local)"};gridFilters[grid]=filter;bar.Controls.Add(filter);
            filter.TextChanged+=(_,_)=>{if(gridSources.TryGetValue(grid,out var source))Populate(grid,source);};
            grid.CellValueNeeded+=(_,e)=>
            {
                if(gridViews.TryGetValue(grid,out var rows) && e.RowIndex>=0 && e.RowIndex<rows.Count)
                {var entry=rows[e.RowIndex];e.Value=RowValues(entry)[e.ColumnIndex];grid.Rows[e.RowIndex].Tag=entry;}
            };            grid.Columns["Sample CRC32"].HeaderCell.ToolTipText="Sample fingerprint of first/last blocks and size; NOT a whole-file integrity check.";            grid.Columns["CRC32"].HeaderCell.ToolTipText="Whole-file CRC32 verified during Sync upload; — means unknown.";
            grid.KeyDown+=(_,e)=>{if(e.Alt && e.KeyCode==Keys.Enter){e.Handled=true;_ = EditMetadata(grid);}};
            if(grid==music)grid.CellDoubleClick+=(_,e)=>{if(e.RowIndex>=0)_ = EditMetadata(grid);};
        }
    }
    IEnumerable<RemoteFileEntry> GridEntries(DataGridView grid)=>gridSources.TryGetValue(grid,out var rows)?rows:[];
    RemoteFileEntry? EntryAt(DataGridView grid,int row)=>grid.VirtualMode && gridViews.TryGetValue(grid,out var rows)?rows.ElementAtOrDefault(row):grid.Rows[row].Tag as RemoteFileEntry;
    IEnumerable<RemoteFileEntry> FilterEntries(DataGridView grid,IEnumerable<RemoteFileEntry> entries)
    {
        string search=gridFilters.TryGetValue(grid,out var filter)?filter.Text.Trim():"";
        return search.Length==0?entries:entries.Where(e=>RowValues(e).Any(v=>v?.ToString()?.Contains(search,StringComparison.OrdinalIgnoreCase)==true));
    }
    static string ModifiedText(long unix)
    {
        if(unix<378691200 || unix>4102444800)return "—"; // invalid/unknown FAT years 1980–1981
        return DateTimeOffset.FromUnixTimeSeconds(unix).LocalDateTime.ToString("g");
    }
    object?[] RowValues(RemoteFileEntry e)
    {
        metadata.TryGetValue(CacheKey(e),out var m);indexedTracks.TryGetValue(e.Path,out var track);
        return [e.Name,e.Directory?"—":DisplayValue.Size(e.Size),e.Directory?"Folder":Path.GetExtension(e.Name).TrimStart('.').ToUpperInvariant(),ModifiedText(e.Modified),
            m?.Title,m?.Artist,m?.Album,m?.AlbumArtist,m?.Year,m?.Duration,m?.Genre,m?.TrackNumber,m?.Codec,
            m?.Bitrate is {} b?b+" kbps":"",m?.SampleRate is {} s?s+" Hz":"",m?.DurationSource,
            track?.FullCrc is >0?track.FullCrc.ToString("X8"):"—",track?.SampleCrc is >0?track.SampleCrc.ToString("X8"):"—",e.Path];
    }
    bool PopulateIndexGrid(DataGridView grid,IEnumerable<RemoteFileEntry> entries)
    {
        var source=entries.ToList();gridSources[grid]=source;
        var view=FilterEntries(grid,SortedEntries(source)).ToList();gridViews[grid]=view;
        if(!dbActive)
        {if(grid.VirtualMode){grid.RowCount=0;grid.VirtualMode=false;}return false;}
        grid.RowCount=0;grid.VirtualMode=true;grid.RowCount=view.Count;grid.Invalidate();return true;
    }
    async Task<bool> LoadIndex(CancellationToken ct,bool refresh=false)
    {
        if(device?.Supports("library_db_v1")!=true || indexUnavailable)return false;
        indexState=refresh?"Index: Updating…":"Index: Loading…";ShowInfo();
        try
        {
            var snapshot=await index.LoadAsync(transport!,libraryDeviceGuid,refresh,new Progress<string>(s=>status.Text=s),ct);
            indexedTracks.Clear();metadata.Clear();foreach(var t in snapshot.Tracks){indexedTracks[t.Path]=t;metadata[CacheKey(t.Entry)]=t.Metadata;}
            dbActive=true;indexState=$"Index: Ready (gen {snapshot.Generation})";ShowInfo();return true;
        }
        catch(Exception e)when(e is InvalidDataException or NotSupportedException)
        {
            log.Write("Fast index unavailable; legacy fallback: "+e.Message);indexUnavailable=true;dbActive=false;
            metadata.Clear();indexedTracks.Clear();index.ClearSession();
            indexState="Index: Needs refresh (legacy fallback)";ShowInfo();return false;
        }
        catch
        {
            index.Invalidate();indexState="Index: Needs refresh";ShowInfo();throw;
        }
    }
    async Task UpdateLibrary(CancellationToken ct)
    {
        await EnsureSync(ct);indexUnavailable=false;index.Invalidate();
        if(device?.Supports("library_db_v1")==true)await LoadIndex(ct,true);
        else {await transport!.ScanAsync(ct);metadata.Clear();}
        await ReadMusicLibrary(ct);await LoadFiles(currentPath,ct);device=await transport!.GetDeviceInfoAsync(ct);ShowInfo();
    }
    async Task AfterFileBatch(CancellationToken ct)
    {
        index.Invalidate();indexUnavailable=false;dbActive=false;metadata.Clear();indexState="Index: Needs refresh";
        if(device?.Supports("library_db_v1")==true)await LoadIndex(ct,true);
        else if(device?.Supports("scan")==true)await transport!.ScanAsync(ct);
        await ReadMusicLibrary(ct);
    }
    async Task InitializeLibraryIdentity(CancellationToken ct)
    {
        libraryDeviceGuid=string.IsNullOrWhiteSpace(device?.DeviceGuid)?null:device.DeviceGuid;
        if(libraryDeviceGuid==null && transport?.IsUsb==true)libraryDeviceGuid=(ports.SelectedItem as PortItem)?.DeviceId;
        if(libraryDeviceGuid==null && transport is WiFiMonoPodTransport wifi && device?.Supports("ota_update_v1")==true)
        {
            try{libraryDeviceGuid=(await wifi.GetOtaInfoAsync(ct)).DeviceId;}
            catch(Exception e)when(e is IOException or NotSupportedException){log.Write("Persistent index cache disabled: "+e.Message);}
        }
        // An address or COM number is not a device identity. No cross-session cache without a stable ID.
    }
    async Task<string?> ReusableLocal(RemoteFileEntry entry,CancellationToken ct)
    {
        if(localCopies.TryGetValue(entry.Path,out var path) && File.Exists(path) && indexedTracks.TryGetValue(entry.Path,out var t) && t.FullCrc!=0 && new FileInfo(path).Length==entry.Size && await Crc32.FileAsync(path,ct)==t.FullCrc)return path;
        return null;
    }
    async Task EditMetadata(DataGridView grid)
    {
        if(busy || transport==null)return;var selected=Selected(grid);if(selected.Length!=1)return;var e=selected[0];if(e.Directory)return;
        if(!MetadataEditing.Supported(e.Path))
        {using var ro=new MetadataEditorForm(e,null,null,metadata.GetValueOrDefault(CacheKey(e)));ro.ShowDialog(this);return;}
        if(device?.Protocol<4 || !transport.IsUsb && device?.Supports("fs_upload")!=true)
        {MessageBox.Show(this,"Metadata writing requires firmware with MPOD1 v4 atomic file replacement. This connection is read-only for metadata.","Read-only metadata");return;}
        if(e.Size>=64*1024*1024 && MessageBox.Show(this,$"Editing this {DisplayValue.Size(e.Size)} file requires a full download and upload. Continue?","Edit Metadata",MessageBoxButtons.OKCancel,MessageBoxIcon.Information)!=DialogResult.OK)return;
        MetadataWork? work=null;EditableTags? tags=null;
        string recovery=Path.Combine(Path.GetDirectoryName(prefs.StoreFileName)!,"metadata-recovery");log.Write("Metadata recovery files: "+recovery);
        bool ready=await Run("Downloading file for metadata editing",async ct=>
        {
            await EnsureSync(ct);var stat=await transport.StatAsync(e.Path,ct);
            work=await MetadataEditing.PrepareAsync(transport,stat,index.Current?.Generation,recovery,await ReusableLocal(stat,ct),TransferStatus(),ct);
            tags=await Task.Run(()=>MetadataEditing.Read(work.OriginalFile),ct);
        });
        if(!ready || work==null || tags==null)return;
        bool committed=false;
        using var dialog=new MetadataEditorForm(e,tags,async(values,progress)=>
        {
            bool ok=await Run("Saving metadata and uploading replacement",async ct=>
            {
                await EnsureSync(ct);if(device!.Supports("clock_sync"))await SyncDeviceClock(ct);work=await MetadataEditing.EditAsync(work,values,ct);
                await MetadataEditing.CommitAsync(transport!,work,progress,ct);committed=true;
            });return ok;
        },metadata.GetValueOrDefault(CacheKey(e)),()=>operation?.Cancel(),indexedTracks.TryGetValue(e.Path,out var crcTrack)?$"Full CRC32: {(crcTrack.FullCrc==0?"—":crcTrack.FullCrc.ToString("X8"))}; Sample CRC32 (fingerprint only): {crcTrack.SampleCrc:X8}":"Full CRC32: unavailable");
        dialog.ShowDialog(this);
        if(committed)await Run("Updating Library after metadata save",async ct=>{await AfterFileBatch(ct);await LoadFiles(currentPath,ct);});
    }
    IProgress<TransferProgress> TransferStatus()=>new Progress<TransferProgress>(p=>{transferBar.Value=(int)Math.Clamp(p.Percent,0,100);status.Text=$"{p.FileName}: {p.Percent:F1}% · {DisplayValue.Size(p.Completed)} / {DisplayValue.Size(p.Total)}";});
    async Task RetryMetadata()
    {
        if(busy || transport==null)return;
        using var picker=new OpenFileDialog{Filter="Prepared metadata operation|operation.json",InitialDirectory=Path.Combine(Path.GetDirectoryName(prefs.StoreFileName)!,"metadata-recovery")};
        if(picker.ShowDialog(this)!=DialogResult.OK)return;
        var work=JsonSerializer.Deserialize<MetadataWork>(await File.ReadAllTextAsync(picker.FileName));if(work==null)return;
        if(MessageBox.Show(this,"Retry atomic replacement of "+work.RemotePath+" using "+work.EditedFile+"?","Retry metadata",MessageBoxButtons.OKCancel)!=DialogResult.OK)return;
        await Run("Retrying prepared metadata upload",async ct=>{await EnsureSync(ct);if(device!.Supports("clock_sync"))await SyncDeviceClock(ct);await MetadataEditing.CommitAsync(transport!,work,TransferStatus(),ct);await AfterFileBatch(ct);await LoadFiles(currentPath,ct);});
    }
}
