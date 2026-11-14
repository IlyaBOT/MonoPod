using MonoPod.Core;
namespace MonoPod.Desktop.Forms;
public sealed class MetadataEditorForm : Form
{
    public MetadataEditorForm(RemoteFileEntry entry,EditableTags? tags,Func<EditableTags,IProgress<TransferProgress>,Task<bool>>? save,AudioMetadata? properties,Action? cancelOperation=null,string crcInfo="")
    {
        Text="Edit Metadata — "+entry.Name;Width=620;Height=450;MinimumSize=new(520,430);StartPosition=FormStartPosition.CenterParent;
        var panel=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=2,Padding=new(15),AutoScroll=true};panel.ColumnStyles.Add(new(SizeType.Absolute,120));panel.ColumnStyles.Add(new(SizeType.Percent,100));
        string[] labels=["Title","Artist","Album","Album Artist","Year","Genre","Track Number"];
        string[] values=tags==null?[properties?.Title??"",properties?.Artist??"",properties?.Album??"",properties?.AlbumArtist??"",properties?.Year??"",properties?.Genre??"",properties?.TrackNumber??""]:[tags.Title,tags.Artist,tags.Album,tags.AlbumArtist,tags.Year,tags.Genre,tags.TrackNumber];
        var edits=new List<TextBox>();for(int i=0;i<labels.Length;i++){panel.Controls.Add(new Label{Text=labels[i],AutoSize=true},0,i);var box=new TextBox{Text=values[i],Dock=DockStyle.Top,ReadOnly=save==null};edits.Add(box);panel.Controls.Add(box,1,i);}
        var info=new Label{AutoSize=true,MaximumSize=new(540,0),Text=$"{entry.Path}\nSize: {DisplayValue.Size(entry.Size)}; Modified: {(entry.Modified<378691200?"—":DateTimeOffset.FromUnixTimeSeconds(entry.Modified).ToString("g"))}\nCodec: {properties?.Codec}; Duration: {properties?.Duration}; Bitrate: {properties?.Bitrate}; Sample rate: {properties?.SampleRate}\n"+(save==null?"This format is read-only. Metadata writing supports MP3 and FLAC.":"Other tags, pictures and track totals are preserved. Recovery copies remain on this PC.")};panel.Controls.Add(info,0,7);panel.SetColumnSpan(info,2);
        info.Text+="\n"+crcInfo;
        var progress=new Label{AutoSize=true};panel.Controls.Add(progress,0,8);panel.SetColumnSpan(progress,2);
        var buttons=new FlowLayoutPanel{AutoSize=true};var ok=new Button{Text="Save",Enabled=save!=null,AutoSize=true};var cancel=new Button{Text="Cancel",DialogResult=DialogResult.Cancel,AutoSize=true};buttons.Controls.AddRange([ok,cancel]);panel.Controls.Add(buttons,1,9);Controls.Add(panel);CancelButton=cancel;
        bool saving=false;FormClosing+=(_,e)=>{if(saving)e.Cancel=true;};
        cancel.Click+=(_,_)=>{if(saving)cancelOperation?.Invoke();};
        ok.Click+=async(_,_)=>
        {
            if(save==null || saving)return;saving=true;ok.Enabled=false;cancel.DialogResult=DialogResult.None;cancel.Text="Cancel transfer";foreach(var box in edits)box.ReadOnly=true;
            try{var v=edits.Select(b=>b.Text).ToArray();if(await save(new(v[0],v[1],v[2],v[3],v[4],v[5],v[6]),new Progress<TransferProgress>(p=>progress.Text=$"{p.Percent:F1}% · {DisplayValue.Size(p.Completed)} / {DisplayValue.Size(p.Total)}"))){saving=false;DialogResult=DialogResult.OK;Close();}}
            finally{saving=false;ok.Enabled=true;cancel.Enabled=true;cancel.Text="Cancel";cancel.DialogResult=DialogResult.Cancel;foreach(var box in edits)box.ReadOnly=false;}
        };
    }
}
