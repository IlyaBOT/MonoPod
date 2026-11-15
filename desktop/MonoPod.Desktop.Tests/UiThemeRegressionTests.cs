using System.Reflection;
using System.Windows.Forms;
using MonoPod.Core;
using MonoPod.Desktop.Forms;
using MonoPod.Desktop.Services;
using MonoPod.Desktop.Themes;
using Xunit;

namespace MonoPod.Desktop.Tests;

[CollectionDefinition("WinForms theme regressions", DisableParallelization = true)]
public sealed class UiThemeRegressionCollection { }

[Collection("WinForms theme regressions")]
public sealed partial class UiThemeRegressionTests
{
    const BindingFlags Private = BindingFlags.Instance | BindingFlags.NonPublic;
    static T Field<T>(object target, string name) => (T)target.GetType().GetField(name, Private)!.GetValue(target)!;
    static void SetField(object target, string name, object? value) => target.GetType().GetField(name, Private)!.SetValue(target, value);
    static object? Invoke(object target, string name, params object?[] args) => target.GetType().GetMethods(Private)
        .Single(method => method.Name == name && method.GetParameters().Length == args.Length &&
            method.GetParameters().Select((parameter,index) => args[index] == null || parameter.ParameterType.IsInstanceOfType(args[index])).All(match => match))
        .Invoke(target,args);
    static void Click(Button button) => typeof(Button).GetMethod("OnClick", Private)!.Invoke(button, [EventArgs.Empty]);

    static ThemeDocument Theme(uint codepoint = 63)
    {
        var document = new ThemeDocument();
        document.Main.Add(new(codepoint, 1, 2, new byte[8]));
        document.Small.Add(new(codepoint, 1, 2, new byte[6]));
        return document;
    }

    // The message loop keeps async WinForms continuations on one STA thread.
    // No form is shown and every Preferences instance writes to a private temp dir.
    static async Task Sta(Func<Task> action)
    {
        var completed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var thread = new Thread(() =>
        {
            Exception? failure = null;
            try
            {
                using var context = new ApplicationContext();
                using var synchronization = new WindowsFormsSynchronizationContext();
                SynchronizationContext.SetSynchronizationContext(synchronization);
                using var start = new System.Windows.Forms.Timer { Interval = 1 };
                start.Tick += async (_, _) =>
                {
                    start.Stop();
                    try { await action(); }
                    catch (Exception error) { failure = error; }
                    finally { context.ExitThread(); }
                };
                start.Start();
                Application.Run(context);
            }
            catch (Exception error) { failure = error; }
            finally
            {
                SynchronizationContext.SetSynchronizationContext(null);
                if (failure == null) completed.TrySetResult();
                else completed.TrySetException(failure);
            }
        }) { IsBackground = true, Name = "MonoPod UI regression STA" };
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
        await completed.Task.WaitAsync(TimeSpan.FromSeconds(30));
    }

    sealed class SettingsScope : IDisposable
    {
        public string Folder { get; } = Path.Combine(Path.GetTempPath(), "MonoPod-UI-" + Guid.NewGuid().ToString("N"));
        public Preferences Preferences { get; }
        public SettingsScope() => Preferences = new Preferences(Folder);
        public void Dispose()
        {
            if (!Directory.Exists(Folder)) return;
            foreach (string file in Directory.EnumerateFiles(Folder)) File.Delete(file);
            Directory.Delete(Folder);
        }
    }

    [Fact]
    public Task ConcurrentRebuildWaitersReceiveCurrentDocument() => Sta(async () =>
    {
        using var settings = new SettingsScope();
        var raster = new TaskCompletionSource<ThemeDocument>(TaskCreationOptions.RunContinuationsAsynchronously);
        int renders = 0;
        using var editor = new ThemeEditorForm(settings.Preferences, (_, _) => Task.CompletedTask,
            () => Task.FromResult<byte[]?>(null), _ => { renders++; return raster.Task; });
        Task first = editor.Rebuild(), second = editor.Rebuild();
        Assert.False(first.IsCompleted);
        Assert.False(second.IsCompleted);
        var expected = Theme();
        raster.SetResult(expected);
        await Task.WhenAll(first, second);
        Assert.Equal(1, renders);
        Assert.Same(expected, Field<ThemeDocument>(editor, "document"));
        Assert.NotNull(Field<PixelPictureBox>(editor, "preview").Image);
        Assert.True(File.Exists(Path.Combine(settings.Folder, "settings.json")));
    });

    [Fact]
    public Task DeviceThemeReadInvalidatesOlderRasterization() => Sta(async () =>
    {
        using var settings = new SettingsScope();
        var raster = new TaskCompletionSource<ThemeDocument>(TaskCreationOptions.RunContinuationsAsynchronously);
        var imported = Theme(65);
        using var editor = new ThemeEditorForm(settings.Preferences, (_, _) => Task.CompletedTask,
            () => Task.FromResult<byte[]?>(imported.Encode()), _ => raster.Task);
        Task oldRaster = editor.Rebuild();
        editor.SetDeviceState(true, true, "Upload Theme");
        Click(editor.ReadButton);
        Assert.Equal(65u, Field<ThemeDocument>(editor, "document").Main.Single().Codepoint);
        raster.SetResult(Theme(63));
        await oldRaster;
        Assert.Equal(65u, Field<ThemeDocument>(editor, "document").Main.Single().Codepoint);
    });

    [Fact]
    public Task UploadAwaitsRasterWhileTimerRebuildIsQueued() => Sta(async () =>
    {
        using var settings = new SettingsScope();
        var raster = new TaskCompletionSource<ThemeDocument>(TaskCreationOptions.RunContinuationsAsynchronously);
        var uploaded = new TaskCompletionSource<byte[]>(TaskCreationOptions.RunContinuationsAsynchronously);
        int uploadCalls = 0;
        using var editor = new ThemeEditorForm(settings.Preferences,
            (bytes, _) => { uploadCalls++; uploaded.TrySetResult(bytes); return Task.CompletedTask; },
            () => Task.FromResult<byte[]?>(null), _ => raster.Task);
        editor.SetDeviceState(true, true, "Upload Theme");
        Click(editor.UploadButton);
        Task timerRebuild = editor.Rebuild();
        Assert.Equal(0, uploadCalls);
        Assert.False(editor.UploadButton.Enabled);
        var expected = Theme(65);
        raster.SetResult(expected);
        byte[] bytes = await uploaded.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await timerRebuild;
        Assert.Equal(expected.Encode(), bytes);
        Assert.Equal(1, uploadCalls);
        Assert.True(editor.UploadButton.Enabled);
    });

    [Fact]
    public Task InvalidDownloadedThemeReturnsNullAndClosesTransport() => Sta(async () =>
    {
        using var settings = new SettingsScope();
        using var form = new MainForm(smoke: true, preferences: settings.Preferences);
        var target = new RecordingTransport { ThemeBytes = new byte[44] };
        SetField(form, "transport", target); SetField(form, "device", target.Info);
        byte[]? bytes = await (Task<byte[]?>)Invoke(form, "ReadTheme")!;
        Assert.Null(bytes);
        Assert.True(target.Disposed);
        Assert.Null(Field<IMonoPodTransport?>(form, "transport"));
    });

    [Fact]
    public Task InstallingThemeKeepsSyncSessionForFurtherTransfers() => Sta(async () =>
    {
        using var settings = new SettingsScope();
        using var form = new MainForm(smoke: true, preferences: settings.Preferences);
        var target = new RecordingTransport();
        SetField(form, "transport", target); SetField(form, "device", target.Info);
        byte[] theme = Theme().Encode();
        await (Task)Invoke(form, "UploadTheme", theme, true)!;
        Assert.Equal(theme, target.UploadedTheme);
        Assert.Equal(1, target.StartCalls);
        Assert.Equal(1, target.SmoothingCalls);
        Assert.Equal(0, target.EndCalls);
        Assert.False(target.Disposed);
        Assert.Same(target, Field<IMonoPodTransport>(form, "transport"));
    });

    [Fact]
    public Task InterruptedPhysicalUsbPathRecoversAndEndsBeforeUiReportsFailure() => Sta(async () =>
    {
        using var settings = new SettingsScope();
        using var form = new MainForm(smoke: true, preferences: settings.Preferences);
        var original = new UsbDevice { SdWriteErrorAt = 1 };
        var fresh = new UsbDevice { Sync = true };
        int opened = 0;
        var usb = new UsbMonoPodTransport(() => ++opened == 1 ? original : fresh,
            recoveryQuietPeriod: TimeSpan.Zero);
        var info = await usb.ConnectAsync(default);
        SetField(form, "transport", usb); SetField(form, "device", info);
        await (Task)Invoke(form, "UploadTheme", Theme().Encode(), true)!;
        Assert.Equal(2, opened);
        Assert.False(fresh.Sync);
        Assert.True(fresh.Disposed);
        Assert.Null(Field<IMonoPodTransport?>(form, "transport"));
        Assert.Contains("MPOD1 SYNC END", fresh.Commands);
        Assert.DoesNotContain("MPOD1 SYNC END", original.Commands);
        Assert.DoesNotContain(fresh.Commands, c=>c.StartsWith("MPOD1 X THEME ",StringComparison.Ordinal));
    });

    [Fact]
    public Task LostHttpSessionClearsConnectionInsteadOfKeepingStaleUi() => Sta(async () =>
    {
        using var settings = new SettingsScope();
        using var form = new MainForm(smoke: true, preferences: settings.Preferences);
        var target = new RecordingTransport { IsUsb = false, FailInfo = true };
        SetField(form, "transport", target); SetField(form, "device", target.Info);
        await (Task)Invoke(form, "Start")!;
        Assert.True(target.Disposed);
        Assert.Null(Field<IMonoPodTransport?>(form, "transport"));
        Assert.Equal("/", Field<string>(form, "currentPath"));
    });

    [Theory]
    [InlineData("SD_WRITE", true)]
    [InlineData("TIMEOUT_OR_DISCONNECT", true)]
    [InlineData("INVALID_THEME_OR_CRC", false)]
    public Task UsbBinaryFailureDetachesUiButSemanticRejectionKeepsSession(string code, bool mustDisconnect) => Sta(async () =>
    {
        using var settings = new SettingsScope();
        using var form = new MainForm(smoke: true, preferences: settings.Preferences);
        var target = new RecordingTransport { UploadError = code };
        SetField(form, "transport", target); SetField(form, "device", target.Info);
        await (Task)Invoke(form, "UploadTheme", Theme().Encode(), true)!;
        Assert.Equal(mustDisconnect, target.Disposed);
        Assert.Equal(mustDisconnect, Field<IMonoPodTransport?>(form, "transport") == null);
        Assert.Null(target.UploadedTheme);
    });

    [Fact]
    public Task EndingWifiDoesNotRequestInfoAfterServerStops() => Sta(async () =>
    {
        using var settings = new SettingsScope();
        using var form = new MainForm(smoke: true, preferences: settings.Preferences);
        var target = new RecordingTransport { IsUsb = false };
        SetField(form, "transport", target); SetField(form, "device", target.Info);
        await (Task)Invoke(form, "End")!;
        Assert.Equal(1, target.EndCalls);
        Assert.Equal(0, target.InfoCalls);
        Assert.True(target.Disposed);
        Assert.Null(Field<IMonoPodTransport?>(form, "transport"));
    });

    sealed class CancelUiUpload(Action cancel) : IProgress<TransferProgress>
    {
        public void Report(TransferProgress p) => cancel();
    }

    [Fact]
    public Task CancelKeepsConnectedUiAndRefreshesFilesAfterUsbReopen() => Sta(async () =>
    {
        using var settings = new SettingsScope();
        using var form = new MainForm(smoke: true, preferences: settings.Preferences);
        var original = new UsbDevice { Sync = true };
        var fresh = new UsbDevice { Sync = true };
        int opens = 0;
        var usb = new UsbMonoPodTransport(() => ++opens == 1 ? original : fresh, recoveryQuietPeriod: TimeSpan.Zero);
        var info = await usb.ConnectAsync(default);
        SetField(form, "transport", usb); SetField(form, "device", info);
        SetField(form, "currentPath", "/Music");
        Invoke(form, "InitializeTree");
        string local = Path.GetTempFileName();
        try
        {
            await File.WriteAllBytesAsync(local, new byte[4096]);
            Func<CancellationToken,Task> action = ct => usb.UploadAsync(local, "/Music/existing.mp3", true,
                new CancelUiUpload(() => Field<CancellationTokenSource>(form,"operation").Cancel()), ct);
            bool success = await (Task<bool>)Invoke(form,"Run","Uploading files",action)!;
            Assert.False(success);
            Assert.Same(usb,Field<IMonoPodTransport?>(form,"transport"));
            Assert.Equal("usb",Field<DeviceInfo>(form,"device").SyncMode);
            Assert.False(fresh.Disposed);
            Assert.False(Field<bool>(form,"busy"));
            Assert.True(Field<Button>(form,"disconnect").Enabled);
            Assert.False(Field<Button>(form,"connect").Enabled);
            Assert.Equal(2,opens);
            Assert.DoesNotContain("MPOD1 SYNC END",fresh.Commands);
            Assert.Contains(fresh.Commands,c=>c.StartsWith("MPOD1 X LIST ",StringComparison.Ordinal));
            Assert.DoesNotContain(fresh.Commands,c=>c.StartsWith("MPOD1 X READ ",StringComparison.Ordinal));
            await usb.EndSyncAsync(default);
        }
        finally { await usb.DisposeAsync(); File.Delete(local); }
    });

    [Fact]
    public Task CancelBeforeBinaryTransferLeavesExistingPortConnected() => Sta(async () =>
    {
        using var settings = new SettingsScope();
        using var form = new MainForm(smoke: true, preferences: settings.Preferences);
        var device = new UsbDevice { Sync = true };
        int opens = 0;
        var usb = new UsbMonoPodTransport(() => { opens++;return device; });
        var info = await usb.ConnectAsync(default);
        SetField(form,"transport",usb); SetField(form,"device",info);
        Func<CancellationToken,Task> action = ct =>
        {
            Field<CancellationTokenSource>(form,"operation").Cancel();
            return Task.FromCanceled(ct);
        };
        await (Task<bool>)Invoke(form,"Run","Preparing upload",action)!;
        Assert.Same(usb,Field<IMonoPodTransport?>(form,"transport"));
        Assert.Equal(1,opens);
        Assert.False(device.Disposed);
        Assert.DoesNotContain("MPOD1 SYNC END",device.Commands);
        await usb.EndSyncAsync(default); await usb.DisposeAsync();
    });

    [Fact]
    public Task ExpandingFoldersStartsSyncBeforeFirstList() => Sta(async () =>
    {
        using var settings = new SettingsScope();
        using var form = new MainForm(smoke: true, preferences: settings.Preferences);
        var target = new RecordingTransport();
        SetField(form, "transport", target); SetField(form, "device", target.Info);
        Invoke(form, "InitializeTree");
        var tree = Field<TreeView>(form, "tree");
        typeof(TreeView).GetMethod("OnBeforeExpand", Private)!.Invoke(tree,
            [new TreeViewCancelEventArgs(tree.Nodes[0], false, TreeViewAction.Expand)]);
        await target.Listed.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Equal(1, target.StartCalls);
        Assert.Equal(new[] { "start", "info", "list" }, target.Operations);
    });

    sealed class RecordingTransport : IMonoPodTransport, IClockTransport
    {
        public string Description => "UI regression transport";
        public bool IsUsb { get; set; } = true;
        public bool Disposed, FailInfo;
        public int StartCalls, EndCalls, InfoCalls, SmoothingCalls, ClockCalls, ScanCalls;
        public byte[] ThemeBytes = Theme().Encode();
        public byte[]? UploadedTheme;
        public string? UploadError;
        public List<string> Operations { get; } = [];
        public TaskCompletionSource Listed { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
        public DeviceInfo Info { get; set; } = new() { Firmware = "0.10.6", Protocol = 4, SdReady = true,
            SyncMode = "usb", Capabilities = ["theme", "theme_get", "smoothing", "read", "stat", "list"] };
        public Task<DeviceInfo> ConnectAsync(CancellationToken ct) => Task.FromResult(Info);
        public Task<DeviceInfo> GetDeviceInfoAsync(CancellationToken ct)
        {
            InfoCalls++; Operations.Add("info");
            return FailInfo ? Task.FromException<DeviceInfo>(new HttpRequestException("Player left Wi-Fi Sync")) : Task.FromResult(Info);
        }
        public Task StartSyncAsync(CancellationToken ct) { StartCalls++; Operations.Add("start"); return Task.CompletedTask; }
        public Task EndSyncAsync(CancellationToken ct) { EndCalls++; return Task.CompletedTask; }
        public Task<IReadOnlyList<RemoteFileEntry>> ListAsync(string path, CancellationToken ct)
        {
            Assert.True(StartCalls > 0); Operations.Add("list"); Listed.TrySetResult();
            return Task.FromResult<IReadOnlyList<RemoteFileEntry>>([]);
        }
        public Task<RemoteFileEntry> StatAsync(string path, CancellationToken ct) => Task.FromResult(new RemoteFileEntry { Path = path, Size = ThemeBytes.Length });
        public Task<byte[]> ReadRangeAsync(string path, long offset, int count, CancellationToken ct) => Task.FromResult(ThemeBytes.Skip((int)offset).Take(count).ToArray());
        public Task UploadThemeAsync(byte[] data, CancellationToken ct)
        {
            if (UploadError != null) return Task.FromException(new DeviceException(UploadError));
            UploadedTheme = data; return Task.CompletedTask;
        }
        public Task SetSmoothingAsync(bool enabled, CancellationToken ct) { SmoothingCalls++; return Task.CompletedTask; }
        public ValueTask DisposeAsync() { Disposed = true; return ValueTask.CompletedTask; }
        public Task UploadAsync(string localPath, string remotePath, bool replace, IProgress<TransferProgress>? progress, CancellationToken ct) => throw new NotSupportedException();
        public Task DownloadAsync(string remotePath, string localPath, IProgress<TransferProgress>? progress, CancellationToken ct) => throw new NotSupportedException();
        public Task DeleteAsync(string path, bool directory, CancellationToken ct) => throw new NotSupportedException();
        public Task CreateDirectoryAsync(string path, CancellationToken ct) => throw new NotSupportedException();
        public Task RenameAsync(string path, string destination, CancellationToken ct) => throw new NotSupportedException();
        public Task ScanAsync(CancellationToken ct) { ScanCalls++; Operations.Add("scan"); return Task.CompletedTask; }
        public Task SetClockAsync(ClockConfiguration clock, CancellationToken ct) { ClockCalls++; Operations.Add("clock"); return Task.CompletedTask; }
    }
}
