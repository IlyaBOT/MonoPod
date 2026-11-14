using MonoPod.Core;
using MonoPod.Desktop.Services;
using System.IO.Ports;
using Microsoft.Win32;

namespace MonoPod.Desktop.Forms;
public sealed partial class MainForm : Form
{
    readonly Preferences prefs;
    readonly AppLog log = new();
    IMonoPodTransport? transport;
    DeviceInfo? device;
    readonly ComboBox connection = new()
    {
        DropDownStyle = ComboBoxStyle.DropDownList,
        Width = 85
    }, ports = new()
    {
        DropDownStyle = ComboBoxStyle.DropDownList,
        Width = 280
    }, address = new()
    {
        Width = 280
    };
    readonly Button connect = new()
    {
        Text = "Connect",
        AutoSize = true
    }, disconnect = new()
    {
        Text = "Disconnect",
        AutoSize = true
    }, refreshPorts = new()
    {
        Text = "Refresh Ports",
        AutoSize = true
    }, start = new()
    {
        Text = "Start Sync",
        AutoSize = true
    }, end = new()
    {
        Text = "End Sync",
        AutoSize = true
    };
    readonly Label details = new()
    {
        Dock = DockStyle.Top,
        Height = 62,
        Padding = new(10, 4, 5, 2),
        AutoEllipsis = true
    }, storage = new()
    {
        AutoSize = true
    }, capabilities = new()
    {
        AutoSize = true
    };
    readonly ProgressBar capacity = new()
    {
        Width = 180,
        Height = 18
    };
    readonly TabControl tabs = new()
    {
        Dock = DockStyle.Fill
    };
    readonly DataGridView files = Grid(), music = Grid();
    readonly TextBox pathBox = new()
    {
        Text = "/",
        Width = 320
    };
    readonly TreeView tree = new()
    {
        Dock = DockStyle.Fill,
        HideSelection = false
    };
    readonly RichTextBox logBox = new()
    {
        Dock = DockStyle.Fill,
        ReadOnly = true,
        Font = new Font("Consolas", 9)
    };
    readonly ToolStripStatusLabel status = new("Ready")
    {
        Spring = true,
        TextAlign = ContentAlignment.MiddleLeft
    }, transportStatus = new("Disconnected");
    readonly ToolStripProgressBar transferBar = new()
    {
        Width = 160
    };
    readonly ToolStripButton cancel = new("Cancel");
    readonly List<(Control Control, string? Capability)> commands = [];
    readonly Dictionary<string, AudioMetadata> metadata = [];
    readonly ThemeEditorForm themeEditor;
    CancellationTokenSource? operation;
    bool busy, closePending, allowClose, resetRequested;
    string currentPath = "/", sortColumn = "Name";
    bool descending;
    List<RemoteFileEntry> current = [];
    bool smoke;
    public MainForm(bool smoke = false, float smokeScale = 1f, Preferences? preferences = null)
    {
        prefs = preferences ?? Preferences.Load();
        index = new LibraryIndex(Path.Combine(Path.GetDirectoryName(prefs.StoreFileName)!,"library-cache"));
        this.smoke = smoke;
        Text = "MonoPod Desktop 1.3.0";
        Width = 1280;
        Height = 820;
        MinimumSize = new(920, 620);
        StartPosition = FormStartPosition.CenterScreen;
        AutoScaleMode = AutoScaleMode.Dpi;
        var menu = new MenuStrip();
        ToolStripMenuItem Menu(string title)
        {
            var m = new ToolStripMenuItem(title);
            menu.Items.Add(m);
            return m;
        }

        void Item(ToolStripMenuItem m, string text, Action action)
        {
            m.DropDownItems.Add(text, null, (_, _) => action());
        }

        var file = Menu("File");
        Item(file, "Upload files…", () => _ = Upload(false));
        Item(file, "Save log…", SaveLog);
        Item(file, "Exit", Close);
        var dev = Menu("Device");
        Item(dev, "Refresh Ports", () => _ = RefreshPorts());
        Item(dev, "Connect", () => _ = Connect());
        Item(dev, "Disconnect", () => _ = Disconnect());
        Item(dev, "Firmware Update…", () => _ = UpdateFirmware());
        Item(dev, "Synchronize time with PC", () => _ = Run("Synchronizing device time", ct => SyncDeviceClock(ct)));
        Item(dev, "Refresh information", () => _ = Run("Reading device information", async ct =>
        {
            Require();
            device = await transport!.GetDeviceInfoAsync(ct);
            ShowInfo();
        }));
        var sync = Menu("Sync");
        Item(sync, "Start Sync", () => _ = Start());
        Item(sync, "End Sync", () => _ = End());
        Item(sync, "Update Library", () => _ = Run("Updating Library", UpdateLibrary));
        Item(sync, "Cancel operation", () => operation?.Cancel());
        var tools = Menu("Tools");
        Item(tools, "Theme editor", () => tabs.SelectedIndex = 2);
        Item(tools, "Settings", () => tabs.SelectedIndex = 3);
        var help = Menu("Help");
        Item(help, "User guide", () => OpenDoc("USER_GUIDE_RU.md"));
        Item(help, "About", () => MessageBox.Show(this, "MonoPod Desktop 1.3.0\r\nNative Windows Forms · .NET 8\r\nMonoPod firmware 0.10.6.6 / MPOD1 v4 + OTA v1\r\nUSB CDC and local HTTP sync.", "About MonoPod"));
        MainMenuStrip = menu;
        var top = new FlowLayoutPanel
        {
            Dock = DockStyle.Top,
            Height = 46,
            Padding = new(10, 6, 5, 0),
            WrapContents = false
        };
        top.Controls.AddRange([new Label { Text = "Connection:", AutoSize = true, Padding = new(0, 6, 0, 0) }, connection, ports, address, refreshPorts, connect, disconnect, start, end]);
        connection.Items.AddRange(["USB", "Wi-Fi"]);
        connection.SelectedIndex = prefs.Transport == "Wi-Fi" ? 1 : 0;
        address.Items.AddRange(prefs.Addresses.Cast<object>().ToArray());
        address.Text = prefs.Address;
        var storageRow = new FlowLayoutPanel
        {
            Dock = DockStyle.Top,
            Height = 29,
            Padding = new(10, 0, 0, 0),
            WrapContents = false
        };
        storageRow.Controls.AddRange([storage, capacity, capabilities]);
        var strip = new StatusStrip();
        strip.Items.AddRange([status, transferBar, cancel, transportStatus]);
        cancel.Click += (_, _) => operation?.Cancel();
        themeEditor = new(prefs, UploadTheme, ReadTheme)
        {
            TopLevel = false,
            FormBorderStyle = FormBorderStyle.None,
            Dock = DockStyle.Fill
        };
        var ftab = new TabPage("Files");
        var mtab = new TabPage("Music");
        var ttab = new TabPage("Themes");
        var stab = new TabPage("Settings");
        var ltab = new TabPage("Log");
        tabs.TabPages.AddRange([ftab, mtab, ttab, stab, ltab]);
        var fsbar = new FlowLayoutPanel
        {
            Dock = DockStyle.Top,
            Height = 100,
            Padding = new(5),
            WrapContents = true
        };
        fsbar.Controls.Add(new Label { Text = "Path:", AutoSize = true, Padding = new(0, 7, 0, 0) });
        fsbar.Controls.Add(pathBox);
        Add(fsbar, "Go", () => _ = Navigate(pathBox.Text), "list");
        Add(fsbar, "Up", () => _ = Navigate(RemotePath.Parent(currentPath)), "list");
        Add(fsbar, "Refresh", () => _ = Navigate(currentPath), "list");
        Add(fsbar, "New Folder…", NewFolder, "mkdir");
        Add(fsbar, "Upload Files…", () => _ = Upload(false), "fs_upload");
        Add(fsbar, "Download…", () => _ = Download(files), "read");
        Add(fsbar, "Rename…", () => Rename(files), "rename");
        Add(fsbar, "Delete…", () => Delete(files), "delete");
        var split = new SplitContainer
        {
            Dock = DockStyle.Fill,
            Width = 1100,
            SplitterDistance = 210,
            FixedPanel = FixedPanel.Panel1
        };
        split.Panel1.Controls.Add(tree);
        split.Panel2.Controls.Add(files);
        ftab.Controls.Add(split);
        ftab.Controls.Add(fsbar);
        var musicBar = new FlowLayoutPanel
        {
            Dock = DockStyle.Top,
            Height = 80,
            Padding = new(5)
        };
        Add(musicBar, "Update Library", () => _ = Run("Updating Library", UpdateLibrary), "list");
        Add(musicBar, "Add Music…", () => _ = Upload(true), "upload");
        Add(musicBar, "Download…", () => _ = Download(music), "read");
        Add(musicBar, "Delete…", () => Delete(music), "delete");
        Add(musicBar, "Read Metadata", () => _ = Run("Reading metadata", ct => ReadMetadata(music, ct)), "read");
        ConfigureLibraryUi(tools, musicBar, fsbar);
        mtab.Controls.Add(music);
        mtab.Controls.Add(musicBar);
        ttab.Controls.Add(themeEditor);
        themeEditor.Show();
        stab.Controls.Add(Settings());
        ltab.Controls.Add(logBox);
        var saveLog = new Button
        {
            Text = "Save log…",
            Dock = DockStyle.Bottom,
            Height = 34
        };
        saveLog.Click += (_, _) => SaveLog();
        ltab.Controls.Add(saveLog);
        Controls.Add(tabs);
        Controls.Add(storageRow);
        Controls.Add(details);
        Controls.Add(top);
        Controls.Add(menu);
        Controls.Add(strip);
        files.CellDoubleClick += (_, e) =>
        {
            if (e.RowIndex >= 0 && EntryAt(files, e.RowIndex) is RemoteFileEntry entry && entry.Directory)
                _ = Navigate(entry.Path);
            else if (e.RowIndex >= 0) _ = EditMetadata(files);
        };
        files.ColumnHeaderMouseClick += (_, e) =>
        {
            sortColumn = files.Columns[e.ColumnIndex].Name;
            descending = files.Columns[e.ColumnIndex].HeaderCell.SortGlyphDirection == SortOrder.Ascending;
            foreach (DataGridViewColumn c in files.Columns)
                c.HeaderCell.SortGlyphDirection = SortOrder.None;
            files.Columns[e.ColumnIndex].HeaderCell.SortGlyphDirection = descending ? SortOrder.Descending : SortOrder.Ascending;
            Populate(files, SortedEntries(current));
        };
        music.ColumnHeaderMouseClick += (_, e) =>
        {
            sortColumn = music.Columns[e.ColumnIndex].Name;
            descending = music.Columns[e.ColumnIndex].HeaderCell.SortGlyphDirection == SortOrder.Ascending;
            foreach (DataGridViewColumn c in music.Columns)
                c.HeaderCell.SortGlyphDirection = SortOrder.None;
            music.Columns[e.ColumnIndex].HeaderCell.SortGlyphDirection = descending ? SortOrder.Descending : SortOrder.Ascending;
            Populate(music, SortedEntries(GridEntries(music)));
        };
        foreach (var grid in new[]
        {
            files,
            music
        }

        )
        {
            var context = new ContextMenuStrip();
            context.Items.Add("Edit Metadata…", null, (_, _) => _ = EditMetadata(grid));
            context.Items.Add("Download…", null, (_, _) => _ = Download(grid));
            context.Items.Add("Rename…", null, (_, _) => Rename(grid));
            context.Items.Add("Delete…", null, (_, _) => Delete(grid));
            grid.ContextMenuStrip = context;
            grid.CellMouseDown += (_, e) =>
            {
                if (e.Button == MouseButtons.Right && e.RowIndex >= 0 && !grid.Rows[e.RowIndex].Selected)
                {
                    grid.ClearSelection();
                    grid.Rows[e.RowIndex].Selected = true;
                }
            };
            foreach (DataGridViewColumn column in grid.Columns)
            {
                string key = grid == files ? "files." : "music.";
                if (prefs.ColumnWidths.TryGetValue(key + column.Name, out int width))
                    column.Width = Math.Clamp(width, 40, 800);
            }
        }

        tree.BeforeExpand += async (_, e) =>
        {
            if (busy || transport == null || e.Node == null || e.Node.Nodes.Count != 1 || e.Node.Nodes[0].Tag != null)
                return;
            await Run("Loading folders", async ct =>
            {
                await EnsureSync(ct);
                var entries = await transport!.ListAsync((string)e.Node.Tag!, ct);
                e.Node.Nodes.Clear();
                foreach (var entry in RemotePath.Sort(entries.Where(x => x.Directory)))
                {
                    var node = new TreeNode(entry.Name)
                    {
                        Tag = entry.Path
                    };
                    node.Nodes.Add("Loading…");
                    e.Node.Nodes.Add(node);
                }
            });
        };
        tree.NodeMouseDoubleClick += (_, e) =>
        {
            if (e.Node.Tag is string path)
                _ = Navigate(path);
        };
        connect.Click += (_, _) => _ = Connect();
        disconnect.Click += (_, _) => _ = Disconnect();
        refreshPorts.Click += (_, _) => _ = RefreshPorts();
        start.Click += (_, _) => _ = Start();
        end.Click += (_, _) => _ = End();
        connection.SelectedIndexChanged += (_, _) => SetState();
        log.Added += line =>
        {
            logBox.AppendText(line + Environment.NewLine);
            if (logBox.TextLength > 1000000)
                logBox.Clear();
        };
        tabs.SelectedIndex = Math.Clamp(prefs.Tab, 0, 4);
        FormClosing += OnClosingSafely;
        Shown += async (_, _) =>
        {
            await RefreshPorts();
            SetState();
            if (smoke)
            {
                if (smokeScale is >= 1f and <= 1.5f && smokeScale != 1f)
                    Scale(new SizeF(smokeScale, smokeScale));
                await themeEditor.Rebuild();
                tabs.SelectedIndex = 0;
                await Task.Delay(500);
                string folder = Environment.GetEnvironmentVariable("MONOPOD_SMOKE_OUTPUT") ?? Path.Combine(AppContext.BaseDirectory, "smoke");
                Directory.CreateDirectory(folder);
                using var bitmap = new Bitmap(Width, Height);
                DrawToBitmap(bitmap, new Rectangle(Point.Empty, Size));
                bitmap.Save(Path.Combine(folder, "main.png"));
                for (int i = 1; i < tabs.TabCount; i++)
                {
                    tabs.SelectedIndex = i;
                    await Task.Delay(100);
                    using var tabImage = new Bitmap(Width, Height);
                    DrawToBitmap(tabImage, new Rectangle(Point.Empty, Size));
                    tabImage.Save(Path.Combine(folder, $"tab-{i}.png"));
                }

                File.WriteAllText(Path.Combine(folder, "ui.txt"), $"Started; tabs={tabs.TabCount}; dpi={DeviceDpi}; filesColumns={files.ColumnCount}; themePalette=12");
                allowClose = true;
                Close();
            }
        };
        ShowInfo();
        SetState();
        log.Write("MonoPod Desktop 1.3.0 started.");
    }

    static DataGridView Grid()
    {
        var grid = new DataGridView
        {
            Dock = DockStyle.Fill,
            ReadOnly = true,
            AllowUserToAddRows = false,
            AllowUserToDeleteRows = false,
            SelectionMode = DataGridViewSelectionMode.FullRowSelect,
            MultiSelect = true,
            RowHeadersVisible = false,
            AutoGenerateColumns = false,
            BackgroundColor = SystemColors.Window
        };
        foreach (string name in new[]
        {
            "Name",
            "Size",
            "Type",
            "Modified",
            "Title",
            "Artist",
            "Album",
            "Album Artist",
            "Year",
            "Duration",
            "Genre",
            "Track Number",
            "Codec/Format",
            "Bitrate",
            "Sample Rate",
            "Duration Source",
            "CRC32",
            "Sample CRC32",
            "Path"
        }

        )
            grid.Columns.Add(new DataGridViewTextBoxColumn { Name = name, HeaderText = name, Width = name == "Name" ? 220 : 120, SortMode = DataGridViewColumnSortMode.Programmatic });
        return grid;
    }

    void Add(Control parent, string text, Action action, string? capability)
    {
        var button = new Button
        {
            Text = text,
            AutoSize = true
        };
        button.Click += (_, _) => action();
        parent.Controls.Add(button);
        commands.Add((button, capability));
    }

    Control Settings()
    {
        var panel = new FlowLayoutPanel
        {
            Dock = DockStyle.Fill,
            FlowDirection = FlowDirection.TopDown,
            Padding = new(20),
            AutoScroll = true,
            WrapContents = false
        };
        var warn = new CheckBox
        {
            Text = "Show USB speed warning",
            Checked = prefs.WarnUsb,
            AutoSize = true
        };
        warn.CheckedChanged += (_, _) =>
        {
            prefs.WarnUsb = warn.Checked;
            prefs.Save();
        };
        panel.Controls.Add(warn);
        panel.Controls.Add(DeviceSettings());
        panel.Controls.Add(new Label { Text = "Configuration: " + prefs.StoreFileName, AutoSize = true });
        panel.Controls.Add(new Label { Text = "Wi-Fi Sync uses the address shown on the player. Start Wi-Fi Sync on the device first.\r\nNative USB CDC is the sync port; USB-UART is the development console.\r\nKeep local HTTP sync on a trusted network.", AutoSize = true });
        var reset = new Button
        {
            Text = "Reset application settings",
            AutoSize = true
        };
        reset.Click += (_, _) =>
        {
            if (MessageBox.Show(this, "Reset local application preferences?", "Reset settings", MessageBoxButtons.OKCancel) == DialogResult.OK)
            {
                var fresh = new Preferences();
                fresh.Save();
                resetRequested = true;
                MessageBox.Show(this, "Defaults saved. The application will close; start it again to load them.");
                Close();
            }
        };
        panel.Controls.Add(reset);
        return panel;
    }

    void SetState()
    {
        bool usb = connection.Text != "Wi-Fi", connected = transport != null;
        ports.Visible = usb;
        address.Visible = !usb;
        refreshPorts.Visible = usb;
        connection.Enabled = !busy && !connected;
        ports.Enabled = address.Enabled = !busy && !connected;
        connect.Enabled = !busy && !connected;
        disconnect.Enabled = !busy && connected;
        refreshPorts.Enabled = !busy;
        start.Enabled = !busy && connected;
        end.Enabled = !busy && connected;
        tree.Enabled = !busy && connected && device?.Supports("list") == true;
        cancel.Enabled = busy;
        tabs.TabPages[3].Enabled = !busy;
        clockSyncButton.Enabled = !busy && connected && device?.Supports("clock_sync") == true;
        foreach (var(control, cap)in commands)
            control.Enabled = !busy && connected && (cap == null || device?.Supports(cap) == true);
        themeEditor.SetDeviceState(!busy && connected && device?.Supports("theme") == true,
            !busy && connected && device?.Supports("theme_get") == true,
            connected && device?.Protocol < 4 ? "Upload Theme (MPT1)" : "Upload Theme");
        if (MainMenuStrip != null)
            foreach (ToolStripMenuItem group in MainMenuStrip.Items)
                foreach (ToolStripItem item in group.DropDownItems)
                {
                    item.Enabled = item.Text switch
                    {
                        "Upload files…" => !busy && connected && device?.Supports("fs_upload") == true,
                        "Connect" => !busy && !connected,
                        "Disconnect" or "Refresh information" or "Start Sync" or "End Sync" => !busy && connected,
                        "Update Library" => !busy && connected && device?.Supports("scan") == true,
                        "Synchronize time with PC" => !busy && connected && device?.Supports("clock_sync") == true,
                        "Firmware Update…" => !busy && transport is WiFiMonoPodTransport && device?.SyncMode == "wifi",
                        "Cancel operation" => busy,
                        "Refresh Ports" => !busy,
                        _ => true
                    };
                }
    }

    void ClearRemoteViews()
    {
        files.RowCount=0; music.RowCount=0;
        gridSources.Clear();gridViews.Clear();indexedTracks.Clear();dbActive=false;indexUnavailable=false;index.ClearSession();
        libraryDeviceGuid=null;indexState="Index: Not loaded";
        tree.Nodes.Clear();
        metadata.Clear();
        current.Clear();
        currentPath = "/";
        pathBox.Text = "/";
    }

    void InitializeTree()
    {
        tree.Nodes.Clear();
        var root = new TreeNode("microSD") { Tag = "/" };
        root.Nodes.Add("Loading…");
        tree.Nodes.Add(root);
    }

    async Task<string?> CloseTransport(bool recoverInterruptedUsb = false)
    {
        var previous = transport;
        transport = null;
        device = null;
        ClearRemoteViews();
        ShowInfo();
        string? recovery = null;
        if (previous != null)
        {
            if (recoverInterruptedUsb && previous is UsbMonoPodTransport { NeedsRecovery: true } usb)
            {
                // The old port has already closed. Wait out its binary receiver;
                // END is sent only after a fresh HELLO, never into upload bytes.
                cancel.Enabled = false;
                status.Text = "Ending Sync after interrupted USB transfer...";
                log.Write(status.Text);
                try
                {
                    using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(90));
                    var progress = new Progress<string>(message =>
                    {
                        status.Text = message;
                        log.Write(message);
                    });
                    await usb.RecoverAndEndSyncAsync(deadline.Token, progress);
                    recovery = "USB Sync ended safely. Verify the target file before retrying.";
                }
                catch (Exception error)
                {
                    recovery = "Could not confirm the end of USB Sync: " + error.Message +
                        " Reconnect and use End Sync before retrying.";
                }
                log.Write(recovery);
            }
            try
            {
                await previous.DisposeAsync();
            }
            catch (Exception error)
            {
                log.Write("Disconnect: " + error.Message);
            }
        }
        return recovery;
    }

    void Require()
    {
        if (transport == null)
            throw new InvalidOperationException("Connect to MonoPod first.");
    }

    async Task<string> RestoreConnectionAfterCancel(UsbMonoPodTransport usb)
    {
        bool resumeSync = device?.SyncMode == "usb";
        cancel.Enabled = false;
        status.Text = "Transfer cancelled; restoring USB connection...";
        log.Write(status.Text);
        using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(90));
        var progress = new Progress<string>(message => { status.Text = message; log.Write(message); });
        device = usb.NeedsRecovery
            ? await usb.RecoverAfterCancellationAsync(resumeSync, deadline.Token, progress)
            : await usb.GetDeviceInfoAsync(deadline.Token);
        ShowInfo();
        metadata.Clear();
        if (device.SyncMode == "usb" && device.Supports("list"))
        {
            // Refresh committed files without starting another metadata transfer
            // immediately after the user cancelled one.
            try { await LoadFiles(currentPath, deadline.Token, readMetadata: false); }
            catch (DeviceException error) when (error.Code is "NOT_FOUND" or "NOT_DIRECTORY")
            {
                InitializeTree();
                await LoadFiles("/", deadline.Token, readMetadata: false);
            }
        }
        return "Transfer cancelled. USB connection kept" + (device.SyncMode == "usb" ? "; Sync remains active." : ".");
    }

    async Task<bool> Run(string text, Func<CancellationToken, Task> action)
    {
        if (busy)
            return false;
        bool succeeded = false;
        busy = true;
        operation = new();
        status.Text = text;
        SetState();
        try
        {
            await action(operation.Token);
            status.Text = "Ready";
            succeeded = true;
        }
        catch (OperationCanceledException)
        {
            bool userCancelled = operation.IsCancellationRequested;
            string? recovery = null;
            if (userCancelled && !closePending && transport is UsbMonoPodTransport usb)
            {
                try { recovery = await RestoreConnectionAfterCancel(usb); }
                catch (Exception error)
                {
                    log.Write("USB reconnect after cancellation: " + error.Message);
                    await CloseTransport();
                    recovery = "Transfer cancelled, but USB could not reconnect: " + error.Message + " Use Connect to continue or end Sync.";
                }
            }
            else if (transport != null && (transport.IsUsb || !userCancelled))
            {
                recovery = await CloseTransport(recoverInterruptedUsb: true);
            }

            status.Text = recovery ?? (userCancelled ? "Cancelled." : "The connection timed out. Reconnect and check that Sync Mode is active.");
            log.Write(status.Text);
            if (!userCancelled && !smoke)
                MessageBox.Show(this, status.Text, "Connection timeout", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
        catch (Exception e)
        {
            status.Text = e.Message;
            log.Write(text + ": " + e.Message);
            string? recovery = null;
            if (transport != null && (e is InvalidDataException or TimeoutException or HttpRequestException ||
                e is IOException and not DeviceException ||
                transport.IsUsb && e is DeviceException { Code: "SD_WRITE" or "TIMEOUT_OR_DISCONNECT" }))
            {
                recovery = await CloseTransport(recoverInterruptedUsb: true);
            }
            status.Text = e.Message + (recovery == null ? "" : " " + recovery);
            if (!smoke)
                MessageBox.Show(this, status.Text, "MonoPod Desktop", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
        finally
        {
            operation.Dispose();
            operation = null;
            busy = false;
            transferBar.Value = 0;
            SetState();
            if (closePending)
                Close();
        }
        return succeeded;
    }

    async Task RefreshPorts()
    {
        if (busy)
            return;
        var names = await Task.Run(() =>
        {
            var found = SerialPort.GetPortNames().OrderBy(x => x, StringComparer.OrdinalIgnoreCase).ToArray();
            var descriptions = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            var identities = new Dictionary<string,string>();
            try
            {
                using var root = Registry.LocalMachine.OpenSubKey(@"SYSTEM\CurrentControlSet\Enum");
                if (root != null)
                    foreach (string bus in new[]
                    {
                        "USB",
                        "FTDIBUS",
                        "ROOT",
                        "BTHENUM",
                        "ACPI"
                    }

                    )
                    {
                        using var b = root.OpenSubKey(bus);
                        if (b == null)
                            continue;
                        foreach (string dev in b.GetSubKeyNames())
                        {
                            using var d = b.OpenSubKey(dev);
                            if (d == null)
                                continue;
                            foreach (string instance in d.GetSubKeyNames())
                            {
                                using var v = d.OpenSubKey(instance);
                                using var parameters = v?.OpenSubKey("Device Parameters");
                                if (parameters?.GetValue("PortName")is string port)
                                    {descriptions[port] = v?.GetValue("FriendlyName") as string ?? "Serial port";
                                    if(bus=="USB" && dev.Contains("VID_303A",StringComparison.OrdinalIgnoreCase) && System.Text.RegularExpressions.Regex.IsMatch(instance,"^[0-9a-fA-F]{12}$|^([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}$")) identities[port]="usb-serial:"+instance.ToUpperInvariant();}
                            }
                        }
                    }
            }
            catch (System.Security.SecurityException)
            {
            }

            return found.Select(x => new PortItem(x, descriptions.GetValueOrDefault(x, "Serial port"),identities.GetValueOrDefault(x))).ToArray();
        });
        ports.Items.Clear();
        ports.Items.AddRange(names);
        ports.SelectedItem = names.FirstOrDefault(x => x.Name == prefs.Port) ?? names.FirstOrDefault();
        log.Write($"Found {names.Length} COM port(s).");
    }

    sealed record PortItem(string Name, string Label, string? DeviceId=null)
    {
        public override string ToString() => Name + " — " + Label;
    }

    async Task Connect() => await Run("Connecting", async ct =>
    {
        IMonoPodTransport next = connection.Text == "Wi-Fi" ? new WiFiMonoPodTransport(address.Text) : new UsbMonoPodTransport((ports.SelectedItem as PortItem)?.Name ?? throw new InvalidOperationException("Select the Native USB CDC COM port."));
        try
        {
            device = await next.ConnectAsync(ct);
            transport = next;
            ClearRemoteViews();
            prefs.Transport = connection.Text;
            prefs.Port = (ports.SelectedItem as PortItem)?.Name ?? prefs.Port;
            prefs.Address = address.Text;
            if (!string.IsNullOrWhiteSpace(address.Text))
            {
                prefs.Addresses.Remove(address.Text);
                prefs.Addresses.Insert(0, address.Text);
                prefs.Addresses = prefs.Addresses.Take(12).ToList();
            }

            prefs.Save();
            ShowInfo();
            log.Write("Connected: " + next.Description + " firmware " + device.Firmware + " protocol " + device.Protocol);
            InitializeTree();
            await InitializeLibraryIdentity(ct);
            await AfterConnection(ct);
            if (device.SyncMode is "wifi" or "usb" && device.Supports("list"))
                await LoadFiles("/", ct);
            else
                details.Text += "\r\nClick Start Sync before accessing microSD.";
        }
        catch
        {
            if (transport != next)
                await next.DisposeAsync();
            throw;
        }
    });
    void ShowInfo()
    {
        if (device == null)
        {
            details.Text = "Disconnected";
            storage.Text = "microSD: —";
            capabilities.Text = "";
            transportStatus.Text = "Disconnected";
            capacity.Value = 0;
            ShowClockInfo();
            return;
        }

        details.Text = $"Device: MonoPod    Firmware: {device.Firmware}    Protocol: {device.Protocol}    Sync: {device.SyncMode}    Tracks: {(dbActive?indexedTracks.Count:device.Tracks)}    {indexState}    IP: {device.Ip}";
        long used = Math.Max(0, device.TotalBytes - device.FreeBytes);
        storage.Text = $"microSD: {DisplayValue.Size(device.TotalBytes)}   Used: {DisplayValue.Size(used)}   Free: {DisplayValue.Size(device.FreeBytes)}";
        capacity.Value = device.TotalBytes > 0 ? (int)Math.Clamp(used * 100d / device.TotalBytes, 0, 100) : 0;
        capabilities.Text = "Capabilities: " + string.Join(", ", device.Capabilities);
        transportStatus.Text = transport?.Description ?? "Disconnected";
        ShowClockInfo();
    }

    async Task Disconnect() => await Run("Disconnecting", async ct =>
    {
        if (transport != null)
        {
            try
            {
                await transport.EndSyncAsync(ct);
            }
            finally
            {
                await CloseTransport();
            }
        }
    });
    async Task EnsureSync(CancellationToken ct)
    {
        Require();
        await transport!.StartSyncAsync(ct);
        device = await transport.GetDeviceInfoAsync(ct);
        ShowInfo();
        if (!device.SdReady)
            throw new DeviceException("NO_SD");
    }

    async Task Start() => await Run("Starting Sync", async ct =>
    {
        await EnsureSync(ct);
        if (device!.Supports("list"))
        {
            if (tree.Nodes.Count == 0)
                InitializeTree();
            await LoadFiles(currentPath, ct);
        }
    });
    async Task End() => await Run("Ending Sync", async ct =>
    {
        Require();
        await transport!.EndSyncAsync(ct);
        if (!transport.IsUsb)
        {
            await CloseTransport();
        }
        else
            device = await transport!.GetDeviceInfoAsync(ct);
        ClearRemoteViews();
        ShowInfo();
    });
    async Task Navigate(string path) => await Run("Reading directory", async ct =>
    {
        await EnsureSync(ct);
        await LoadFiles(RemotePath.Normalize(path), ct);
    });
    async Task LoadFiles(string path, CancellationToken ct, bool readMetadata = true)
    {
        if (readMetadata) await LoadIndex(ct);
        current = (await transport!.ListAsync(path, ct)).Select(e => indexedTracks.TryGetValue(e.Path,out var t) && dbActive ? t.Entry : e).ToList();
        currentPath = path;
        pathBox.Text = path;
        Populate(files, SortedEntries(current));
        RefreshTreeNode(path, current);
        if (readMetadata) await ReadMetadata(files, ct);
    }

    void RefreshTreeNode(string path, IEnumerable<RemoteFileEntry> entries)
    {
        var pending = new Stack<TreeNode>(tree.Nodes.Cast<TreeNode>());
        while (pending.Count > 0)
        {
            var node = pending.Pop();
            if (node.Tag is string p && p == path)
            {
                node.Nodes.Clear();
                foreach (var e in RemotePath.Sort(entries.Where(x => x.Directory)))
                {
                    var child = new TreeNode(e.Name)
                    {
                        Tag = e.Path
                    };
                    child.Nodes.Add("Loading…");
                    node.Nodes.Add(child);
                }

                return;
            }

            foreach (TreeNode child in node.Nodes)
                pending.Push(child);
        }
    }

    static bool Audio(string path) => new[]
    {
        ".mp3",
        ".wav",
        ".flac",
        ".aac"
    }.Contains(Path.GetExtension(path).ToLowerInvariant());
    IEnumerable<RemoteFileEntry> SortedEntries(IEnumerable<RemoteFileEntry> entries)
    {
        IComparable Key(RemoteFileEntry e)
        {
            metadata.TryGetValue(CacheKey(e), out var m);
            return sortColumn switch
            {
                "Size" => e.Size,
                "Modified" => e.Modified,
                "Type" => Path.GetExtension(e.Name).ToUpperInvariant(),
                "Title" => m?.Title.ToUpperInvariant() ?? "",
                "Artist" => m?.Artist.ToUpperInvariant() ?? "",
                "Album" => m?.Album.ToUpperInvariant() ?? "",
                "Album Artist" => m?.AlbumArtist.ToUpperInvariant() ?? "",
                "Year" => m?.Year ?? "",
                "Duration" => m?.Seconds ?? -1d,
                "Genre" => m?.Genre.ToUpperInvariant() ?? "",
                "Track Number" => int.TryParse(m?.TrackNumber.Split('/')[0], out int n) ? n : 0,
                "Codec/Format" => m?.Codec ?? "",
                "Bitrate" => m?.Bitrate ?? 0,
                "Sample Rate" => m?.SampleRate ?? 0,
                "Duration Source" => m?.DurationSource ?? "",
                "Path" => e.Path,
                "CRC32" => indexedTracks.GetValueOrDefault(e.Path)?.FullCrc ?? 0,
                "Sample CRC32" => indexedTracks.GetValueOrDefault(e.Path)?.SampleCrc ?? 0,
                _ => e.Name.ToUpperInvariant()};
        }

        var groups = entries.OrderBy(x => !x.Directory);
        return descending ? groups.ThenByDescending(Key).ThenBy(x => x.Name, StringComparer.OrdinalIgnoreCase) : groups.ThenBy(Key).ThenBy(x => x.Name, StringComparer.OrdinalIgnoreCase);
    }

    void Populate(DataGridView grid, IEnumerable<RemoteFileEntry> entries)
    {
        if(PopulateIndexGrid(grid,entries)) return;
        entries=FilterEntries(grid,gridSources[grid]);
        grid.Rows.Clear();
        foreach (var entry in entries)
        {
            int index = grid.Rows.Add(entry.Name, entry.Directory ? "—" : DisplayValue.Size(entry.Size), entry.Directory ? "Folder" : Path.GetExtension(entry.Name).TrimStart('.').ToUpperInvariant(), ModifiedText(entry.Modified));
            grid.Rows[index].Tag = entry;
            grid.Rows[index].Cells[18].Value=entry.Path;
            if (metadata.TryGetValue(CacheKey(entry), out var m))
                MetadataRow(grid.Rows[index], m);
        }
    }

    static string CacheKey(RemoteFileEntry e) => e.Path + "|" + e.Size + "|" + e.Modified;
    static void MetadataRow(DataGridViewRow row, AudioMetadata m)
    {
        object? [] values = [m.Title, m.Artist, m.Album, m.AlbumArtist, m.Year, m.Duration, m.Genre, m.TrackNumber, m.Codec, m.Bitrate is { } b ? b + " kbps" : "", m.SampleRate is { } s ? s + " Hz" : "", m.DurationSource];
        for (int i = 0; i < values.Length; i++)
            row.Cells[i + 4].Value = values[i];
        row.ErrorText = m.Note;
    }

    async Task ReadMetadata(DataGridView grid, CancellationToken ct)
    {
        Require();
        if(dbActive) { Populate(grid,GridEntries(grid));return; }
        if (device?.Supports("read") != true)
            return;
        foreach (DataGridViewRow row in grid.Rows.Cast<DataGridViewRow>().ToArray())
        {
            ct.ThrowIfCancellationRequested();
            if (row.Tag is not RemoteFileEntry e || e.Directory || !Audio(e.Name))
                continue;
            string key = CacheKey(e);
            if (!metadata.TryGetValue(key, out var m))
            {
                m = await MetadataReader.ReadAsync(e.Name, e.Size, (offset, count) => transport!.ReadRangeAsync(e.Path, offset, count, ct), ct);
                metadata[key] = m;
            }

            var visibleRow = grid.Rows.Cast<DataGridViewRow>().FirstOrDefault(r => r.Tag is RemoteFileEntry f && f.Path == e.Path);
            if (visibleRow != null)
                MetadataRow(visibleRow, m);
            status.Text = "Metadata: " + e.Name;
        }
    }

    async Task LoadMusic() => await Run("Reading music library", async ct =>
    {
        await EnsureSync(ct);
        await ReadMusicLibrary(ct);
    });
    async Task ReadMusicLibrary(CancellationToken ct)
    {
        if(await LoadIndex(ct)) { Populate(music,SortedEntries(indexedTracks.Values.Select(t=>t.Entry)));return; }
        var all = new List<RemoteFileEntry>();
        var folders = new Queue<string>();
        folders.Enqueue("/Music");
        while (folders.Count > 0)
        {
            ct.ThrowIfCancellationRequested();
            string path = folders.Dequeue();
            foreach (var e in await transport!.ListAsync(path, ct))
            {
                if (e.Directory)
                    folders.Enqueue(e.Path);
                else if (Audio(e.Name))
                    all.Add(e);
            }

            status.Text = $"Found {all.Count} audio files";
        }

        Populate(music, RemotePath.Sort(all));
        await ReadMetadata(music, ct);
    }
    RemoteFileEntry[] Selected(DataGridView grid) => grid.SelectedRows.Cast<DataGridViewRow>().OrderBy(x => x.Index).Select(x => EntryAt(grid,x.Index)).OfType<RemoteFileEntry>().ToArray();
    async Task Upload(bool musicOnly)
    {
        if (busy || transport == null)
            return;
        using var picker = new OpenFileDialog
        {
            Multiselect = true,
            Filter = musicOnly ? "Supported audio|*.mp3;*.wav;*.flac;*.aac" : "All files|*.*",
            InitialDirectory = prefs.LocalFolder
        };
        if (picker.ShowDialog(this) != DialogResult.OK)
            return;
        if (transport.IsUsb && !Dialogs.UsbWarning(this, prefs))
            return;
        string[] local = picker.FileNames;
        prefs.LocalFolder = Path.GetDirectoryName(local[0]) ?? "";
        prefs.Save();
        await Run("Uploading files", async ct =>
        {
            await EnsureSync(ct);
            if (musicOnly && local.Any(f => !device!.AudioFormats.Contains(Path.GetExtension(f), StringComparer.OrdinalIgnoreCase)))
                throw new NotSupportedException("This firmware does not support one of the selected audio formats. AAC in M4A / ALAC is not supported.");
            if (!transport!.IsUsb && !device!.Supports("stat") && MessageBox.Show(this, "This legacy firmware cannot check file conflicts. Uploading to /Music may replace existing names. Continue?", "Legacy Wi-Fi upload", MessageBoxButtons.OKCancel, MessageBoxIcon.Warning) != DialogResult.OK)
                return;
            string dest = musicOnly ? "/Music" : currentPath;
            if (musicOnly && device!.Supports("mkdir"))
            {
                try
                {
                    await transport!.StatAsync(dest, ct);
                }
                catch (DeviceException e)when (e.Message.Contains("NOT_FOUND"))
                {
                    MarkIndexDirty();
            await transport!.CreateDirectoryAsync(dest, ct);
                }
            }

            if (musicOnly)
            {
                var rows = new List<ImportRow>();
                foreach (string f in local)
                {
                    var m = await MetadataReader.LocalAsync(f, ct);
                    rows.Add(new ImportRow(Path.GetFileName(f), m.Title, m.Artist, m.Album, m.AlbumArtist, m.Year, m.Duration, m.Genre, m.TrackNumber, m.Codec, m.Bitrate, m.SampleRate, DisplayValue.Size(new FileInfo(f).Length)));
                }

                using var review = new Form
                {
                    Text = "Review music import",
                    Width = 1080,
                    Height = 430,
                    StartPosition = FormStartPosition.CenterParent
                };
                var grid = new DataGridView
                {
                    Dock = DockStyle.Fill,
                    ReadOnly = true,
                    DataSource = rows,
                    AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.DisplayedCells
                };
                var ok = new Button
                {
                    Text = "Continue import",
                    DialogResult = DialogResult.OK,
                    Dock = DockStyle.Bottom,
                    Height = 35
                };
                review.Controls.Add(grid);
                review.Controls.Add(ok);
                if (review.ShowDialog(this) != DialogResult.OK)
                    return;
            }

            if(device!.Supports("clock_sync")) await SyncDeviceClock(ct);
            MarkIndexDirty();
            using var queue = new TransferProgressForm(local);
            queue.CancelRequested += () => operation?.Cancel();
            queue.Show(this);
            string? policy = null;
            try
            {
                for (int i = 0; i < local.Length; i++)
                {
                    ct.ThrowIfCancellationRequested();
                    string remote = RemotePath.Join(dest, Path.GetFileName(local[i]));
                    bool exists = false;
                    if (device!.Supports("stat"))
                    {
                        try
                        {
                            var stat = await transport!.StatAsync(remote, ct);
                            if (stat.Directory)
                                throw new IOException("Destination is a directory: " + remote);
                            exists = true;
                        }
                        catch (DeviceException e)when (e.Message.Contains("NOT_FOUND"))
                        {
                        }
                    }

                    bool replace = false;
                    if (exists)
                    {
                        var answer = policy == null ? Dialogs.Conflict(this, remote) : (policy, false);
                        if (answer.Item2)
                            policy = answer.Item1;
                        if (answer.Item1 == "Cancel")
                        {
                            operation!.Cancel();
                            throw new OperationCanceledException(ct);
                        }
                        if (answer.Item1 == "Skip")
                        {
                            queue.UpdateRow(i, "Skipped");
                            continue;
                        }

                        replace = true;
                    }

                    int index = i;
                    queue.UpdateRow(index, "Uploading");
                    var progress = new Progress<TransferProgress>(p =>
                    {
                        queue.UpdateRow(index, "Uploading", p);
                        transferBar.Value = (int)Math.Clamp(p.Percent, 0, 100);
                        status.Text = $"{p.FileName}: {p.Percent:F1}% · {DisplayValue.Size((long)p.BytesPerSecond)}/s";
                    });
                    try
                    {
                        await transport!.UploadAsync(local[i], remote, replace, progress, ct);
                        localCopies[remote]=local[i];
                        queue.UpdateRow(index, "Saved");
                        log.Write("Saved " + remote);
                    }
                    catch (DeviceException error)when (error.Message.Contains("ALREADY_EXISTS") && !device.Supports("stat"))
                    {
                        var answer = policy == null ? Dialogs.Conflict(this, remote) : (policy, false);
                        if (answer.Item2)
                            policy = answer.Item1;
                        if (answer.Item1 == "Cancel")
                        {
                            operation!.Cancel();
                            throw new OperationCanceledException(ct);
                        }
                        if (answer.Item1 == "Skip")
                        {
                            queue.UpdateRow(index, "Skipped");
                            continue;
                        }

                        await transport!.UploadAsync(local[i], remote, true, progress, ct);
                        queue.UpdateRow(index, "Saved");
                        log.Write("Replaced " + remote);
                    }
                    catch
                    {
                        queue.UpdateRow(index, "Interrupted / failed");
                        throw;
                    }
                }

                await AfterFileBatch(ct);
                device = await transport.GetDeviceInfoAsync(ct);
                ShowInfo();
                if (device.Supports("list"))
                    await LoadFiles(currentPath, ct);
            }
            finally
            {
                queue.Complete();
            }
        });
    }

    async Task Download(DataGridView grid)
    {
        if (busy || transport == null)
            return;
        var selected = Selected(grid);
        if (selected.Length == 0)
            return;
        if (selected.Any(x => x.Directory))
        {
            MessageBox.Show(this, "Select files. Folder download is Unsupported; open the folder and select its files.");
            return;
        }

        using var picker = new FolderBrowserDialog
        {
            Description = "Download selected files to",
            InitialDirectory = prefs.LocalFolder
        };
        if (picker.ShowDialog(this) != DialogResult.OK)
            return;
        await Run("Downloading files", async ct =>
        {
            await EnsureSync(ct);
            foreach (var e in selected)
            {
                string local = Downloads.LocalTarget(picker.SelectedPath, e.Name);
                if (File.Exists(local) && MessageBox.Show(this, "Replace " + local + "?", "Download", MessageBoxButtons.OKCancel) != DialogResult.OK)
                    continue;
                await transport!.DownloadAsync(e.Path, local, new Progress<TransferProgress>(p =>
                {
                    transferBar.Value = (int)Math.Clamp(p.Percent, 0, 100);
                    status.Text = $"{p.FileName}: {p.Percent:F1}% · {DisplayValue.Size((long)p.BytesPerSecond)}/s";
                }), ct);
                localCopies[e.Path]=local;
                log.Write("Downloaded " + e.Path);
            }

            prefs.LocalFolder = picker.SelectedPath;
            prefs.Save();
        });
    }

    void NewFolder()
    {
        if (busy)
            return;
        string? name = Dialogs.Text(this, "New folder", "Folder name");
        if (string.IsNullOrWhiteSpace(name))
            return;
        _ = Run("Creating directory", async ct =>
        {
            await EnsureSync(ct);
            MarkIndexDirty();
            await transport!.CreateDirectoryAsync(RemotePath.Join(currentPath, name), ct);
            await AfterFileBatch(ct);
            await LoadFiles(currentPath, ct);
        });
    }

    void Rename(DataGridView grid)
    {
        if (busy)
            return;
        var e = Selected(grid).FirstOrDefault();
        if (e == null)
            return;
        string? name = Dialogs.Text(this, "Rename", "New name", e.Name);
        if (string.IsNullOrWhiteSpace(name) || name == e.Name)
            return;
        _ = Run("Renaming", async ct =>
        {
            await EnsureSync(ct);
            MarkIndexDirty();
            await transport!.RenameAsync(e.Path, RemotePath.Join(RemotePath.Parent(e.Path), name), ct);
            await AfterFileBatch(ct);
            await LoadFiles(currentPath, ct);

        });
    }

    void Delete(DataGridView grid)
    {
        if (busy)
            return;
        var selected = Selected(grid);
        if (selected.Length == 0 || MessageBox.Show(this, "Permanently delete these microSD entries? Folders must be empty.\r\n\r\n" + string.Join("\r\n", selected.Take(12).Select(x => x.Path)), "Delete from microSD", MessageBoxButtons.OKCancel, MessageBoxIcon.Warning) != DialogResult.OK)
            return;
        _ = Run("Deleting", async ct =>
        {
            await EnsureSync(ct);
            MarkIndexDirty();
            foreach (var e in selected)
                await transport!.DeleteAsync(e.Path, e.Directory, ct);
            await AfterFileBatch(ct);
            await LoadFiles(currentPath, ct);

            device = await transport!.GetDeviceInfoAsync(ct);
            ShowInfo();
        });
    }

    async Task<byte[]?> ReadTheme()
    {
        byte[]? bytes = null;
        bool succeeded = await Run("Reading device theme", async ct =>
        {
            await EnsureSync(ct);
            const string path = "/MonoPod/theme.mpt";
            var stat = await transport!.StatAsync(path, ct);
            if (stat.Directory || stat.Size < 28 || stat.Size > ThemeDocument.MaxPayload + 44)
                throw new InvalidDataException("Theme exceeds maximum size.");
            using var result = new MemoryStream();
            while (result.Length < stat.Size)
            {
                var data = await transport.ReadRangeAsync(path, result.Length, (int)Math.Min(16384, stat.Size - result.Length), ct);
                if (data.Length == 0)
                    throw new EndOfStreamException();
                result.Write(data);
            }

            var downloaded = result.ToArray();
            ThemeDocument.Decode(downloaded);
            bytes = downloaded;
        });
        return succeeded ? bytes : null;
    }

    async Task UploadTheme(byte[] bytes, bool smooth)
    {
        if (busy)
            throw new InvalidOperationException("Another device operation is active. Finish or cancel it before installing the theme.");
        Require();
        await Run("Installing theme", async ct =>
        {
            await EnsureSync(ct);
            ThemeDocument.Decode(bytes);
            await transport!.UploadThemeAsync(bytes, ct);
            log.Write("Theme installed; device verified theme CRC.");
            if (device!.Supports("smoothing"))
            {
                try
                {
                    await transport.SetSmoothingAsync(smooth, ct);
                }
                catch (DeviceException error)
                {
                    log.Write("Theme installed, but font smoothing was not changed: " + error.Message);
                }
            }
            device = await transport.GetDeviceInfoAsync(ct);
            ShowInfo();
            log.Write("Sync remains active. Use End Sync when finished.");
        });
    }

    sealed record ImportRow(string File, string Title, string Artist, string Album, string AlbumArtist, string Year, string Duration, string Genre, string TrackNumber, string Codec, int? Bitrate, int? SampleRate, string Size);
    void SaveLog()
    {
        using var dialog = new SaveFileDialog
        {
            Filter = "Text log|*.txt",
            FileName = "MonoPod-Desktop.log.txt"
        };
        if (dialog.ShowDialog(this) == DialogResult.OK)
            log.Save(dialog.FileName);
    }

    void OpenDoc(string name)
    {
        string file = Path.Combine(AppContext.BaseDirectory, "docs", name);
        if (!File.Exists(file))
            file = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "docs", name));
        if (File.Exists(file))
            System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(file) { UseShellExecute = true });
        else
            MessageBox.Show(this, "See the docs folder in MonoPod_Desktop.");
    }

    async void OnClosingSafely(object? sender, FormClosingEventArgs e)
    {
        if (allowClose)
            return;
        e.Cancel = true;
        if (busy)
        {
            if (MessageBox.Show(this, "Cancel the active operation safely and close?", "Operation active", MessageBoxButtons.OKCancel, MessageBoxIcon.Warning) == DialogResult.OK)
            {
                closePending = true;
                operation?.Cancel();
            }

            return;
        }

        busy = true;
        operation = new();
        status.Text = "Ending Sync safely...";
        SetState();
        try
        {
            if (!resetRequested)
            {
                prefs.Transport = connection.Text;
                prefs.Port = (ports.SelectedItem as PortItem)?.Name ?? prefs.Port;
                prefs.Address = address.Text;
                prefs.Tab = tabs.SelectedIndex;
                foreach (var grid in new[]
                {
                    files,
                    music
                }

                )
                    foreach (DataGridViewColumn c in grid.Columns)
                        prefs.ColumnWidths[(grid == files ? "files." : "music.") + c.Name] = c.Width;
                prefs.Save();
            }

            if (transport != null)
                await transport.EndSyncAsync(operation.Token);
        }
        catch (Exception error)
        {
            log.Write("Close: " + error.Message);
        }
        finally
        {
            if (transport != null)
            {
                try
                {
                    await transport.DisposeAsync();
                }
                catch (Exception error)
                {
                    log.Write("Disconnect: " + error.Message);
                }

                transport = null;
            }

            operation.Dispose();
            operation = null;
            busy = false;
            allowClose = true;
            Close();
        }
    }
}
