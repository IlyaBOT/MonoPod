using System.Windows.Forms;
using MonoPod.Core;
using MonoPod.Desktop.Forms;
using Xunit;
namespace MonoPod.Desktop.Tests;
public sealed partial class UiThemeRegressionTests
{
    [Fact]public Task DbGridIsVirtualAndFiltersLocallyAndRefreshesOncePerBatch()=>Sta(async()=>
    {
        using var settings=new SettingsScope();using var form=new MainForm(smoke:true,preferences:settings.Preferences);
        var link=new UsbDevice{Database=LibraryDatabaseTests.Database()};await using var usb=new UsbMonoPodTransport(()=>link);
        await usb.ConnectAsync(default);await usb.StartSyncAsync(default);SetField(form,"transport",usb);SetField(form,"device",await usb.GetDeviceInfoAsync(default));
        await (Task)Invoke(form,"ReadMusicLibrary",CancellationToken.None)!;
        var grid=Field<DataGridView>(form,"music");Assert.True(grid.VirtualMode);Assert.Equal(54,grid.RowCount);
        Assert.Equal("—",grid.Rows[0].Cells["Modified"].Value);Assert.Equal("—",grid.Rows[0].Cells["CRC32"].Value);
        Assert.Equal("Русский+%\t\"",grid.Rows[0].Cells["Title"].Value);
        int before=link.Commands.Count;var filters=Field<Dictionary<DataGridView,TextBox>>(form,"gridFilters");filters[grid].Text="Тест+% 12";
        Assert.Equal(1,grid.RowCount);Assert.Equal(before,link.Commands.Count);filters[grid].Clear();
        await (Task)Invoke(form,"AfterFileBatch",CancellationToken.None)!;
        Assert.Equal(1,link.Commands.Count(c=>c=="MPOD1 X DBREFRESH"));Assert.Equal(54,grid.RowCount);
        Assert.DoesNotContain(link.Commands,c=>c.StartsWith("MPOD1 X READ "));
        if(Environment.GetEnvironmentVariable("MONOPOD_UI_TEST_OUTPUT") is {} output)
        {
            Directory.CreateDirectory(output);Field<TabControl>(form,"tabs").SelectedIndex=1;
            SetField(form,"smoke",false);form.ShowInTaskbar=false;form.Opacity=0;form.Show();await Task.Delay(150);
            Invoke(form,"SetState");
            using(var bitmap=new System.Drawing.Bitmap(form.Width,form.Height)){form.DrawToBitmap(bitmap,new(System.Drawing.Point.Empty,form.Size));bitmap.Save(Path.Combine(output,"library-db.png"));}
            using var editor=new MetadataEditorForm(new(){Name="Example.flac",Path="/Music/Example.flac",Size=274412339},new("Title","Artist","Album","Album Artist","2026","Rock","1"),(_,_)=>Task.FromResult(true),new(){Codec="FLAC",Bitrate=900,SampleRate=44100,Seconds=120});
            editor.ShowInTaskbar=false;editor.Opacity=0;editor.Show(form);await Task.Delay(75);
            using(var bitmap=new System.Drawing.Bitmap(editor.Width,editor.Height)){editor.DrawToBitmap(bitmap,new(System.Drawing.Point.Empty,editor.Size));bitmap.Save(Path.Combine(output,"metadata-editor.png"));}
            editor.Hide();form.Hide();
        }
    });
}
