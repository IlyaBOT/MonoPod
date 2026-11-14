using MonoPod.Core;
using MonoPod.Desktop.Themes;
using System.Text.Json;

namespace MonoPod.Desktop.Forms;
public sealed class ThemeEditorForm : Form
{
    readonly PixelPictureBox preview = new()
    {
        Width = 480,
        Height = 384,
        SizeMode = PictureBoxSizeMode.StretchImage,
        BorderStyle = BorderStyle.FixedSingle
    };
    readonly TextBox fontPath = new()
    {
        Width = 350,
        ReadOnly = true
    };
    readonly NumericUpDown mainSize = new()
    {
        Minimum = 8,
        Maximum = 20,
        Value = 15,
        Width = 70
    }, smallSize = new()
    {
        Minimum = 8,
        Maximum = 16,
        Value = 11,
        Width = 70
    };
    readonly CheckBox smoothing = new()
    {
        Text = "Font smoothing",
        AutoSize = true
    };
    readonly ComboBox screen = new()
    {
        DropDownStyle = ComboBoxStyle.DropDownList,
        Width = 200
    };
    readonly TextBox[] colorBoxes = new TextBox[12];
    readonly System.Windows.Forms.Timer timer = new()
    {
        Interval = 250
    };
    readonly Services.Preferences prefs;
    readonly Func<byte[], bool, Task> upload;
    readonly Func<Task<byte[]?>> readTheme;
    readonly Func<ThemeProfile, Task<ThemeDocument>> rasterize;
    public Button ReadButton { get; } = new()
    {
        Text = "Read Device Theme",
        AutoSize = true
    };

    ThemeDocument? document;
    bool rasterNeeded = true, loading;
    bool actionBusy, canUpload, canRead;
    readonly SemaphoreSlim rebuildGate = new(1, 1);
    int generation;
    public Button UploadButton { get; } = new()
    {
        Text = "Upload Theme",
        AutoSize = true
    };

    public ThemeEditorForm(Services.Preferences prefs, Func<byte[], bool, Task> upload, Func<Task<byte[]?>> readTheme,
        Func<ThemeProfile, Task<ThemeDocument>>? rasterize = null)
    {
        this.prefs = prefs;
        this.upload = upload;
        this.readTheme = readTheme;
        this.rasterize = rasterize ?? (profile => Task.Run(() => ThemeRasterizer.Create(profile)));
        Text = "Theme editor";
        AutoScaleMode = AutoScaleMode.Dpi;
        Width = 1060;
        Height = 690;
        var outer = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 2
        };
        outer.ColumnStyles.Add(new(SizeType.Percent, 55));
        outer.ColumnStyles.Add(new(SizeType.Percent, 45));
        var left = new FlowLayoutPanel
        {
            Dock = DockStyle.Fill,
            FlowDirection = FlowDirection.TopDown,
            WrapContents = false,
            AutoScroll = true,
            Padding = new(10)
        };
        var right = new FlowLayoutPanel
        {
            Dock = DockStyle.Fill,
            FlowDirection = FlowDirection.TopDown,
            WrapContents = false,
            AutoScroll = true,
            Padding = new(10)
        };
        outer.Controls.Add(left);
        outer.Controls.Add(right);
        Controls.Add(outer);
        var fontRow = new FlowLayoutPanel
        {
            AutoSize = true
        };
        var browse = new Button
        {
            Text = "Font TTF/OTF…",
            AutoSize = true
        };
        fontRow.Controls.AddRange([fontPath, browse]);
        left.Controls.Add(fontRow);
        var sizes = new FlowLayoutPanel
        {
            AutoSize = true
        };
        sizes.Controls.AddRange([new Label { Text = "Main", AutoSize = true }, mainSize, new Label { Text = "Small", AutoSize = true }, smallSize, smoothing]);
        left.Controls.Add(sizes);
        var colors = new TableLayoutPanel
        {
            AutoSize = true,
            ColumnCount = 3
        };
        for (int i = 0; i < 12; i++)
        {
            int index = i;
            var box = new TextBox
            {
                Width = 90
            };
            colorBoxes[i] = box;
            var choose = new Button
            {
                Text = "Choose…",
                AutoSize = true
            };
            colors.Controls.Add(new Label { Text = ThemeDocument.ColorNames[i], AutoSize = true, Padding = new(0, 6, 8, 0) }, 0, i);
            colors.Controls.Add(box, 1, i);
            colors.Controls.Add(choose, 2, i);
            box.TextChanged += (_, _) =>
            {
                if (!loading)
                    Queue(false);
            };
            choose.Click += (_, _) =>
            {
                using var dialog = new ColorDialog
                {
                    FullOpen = true,
                    Color = ParseColor(box.Text) ?? Color.Black
                };
                if (dialog.ShowDialog(this) == DialogResult.OK)
                    box.Text = $"#{dialog.Color.R:X2}{dialog.Color.G:X2}{dialog.Color.B:X2}";
            };
        }

        left.Controls.Add(colors);
        var actions = new FlowLayoutPanel
        {
            AutoSize = true,
            Width = 540
        };
        void Button(string title, Action click)
        {
            var b = new Button
            {
                Text = title,
                AutoSize = true
            };
            b.Click += (_, _) =>
            {
                try
                {
                    click();
                }
                catch (Exception e)
                {
                    MessageBox.Show(this, e.Message, "Theme", MessageBoxButtons.OK, MessageBoxIcon.Error);
                }
            };
            actions.Controls.Add(b);
        }

        Button("Import MPT…", Import);
        Button("Export MPT…", Export);
        Button("Load profile…", LoadProfile);
        Button("Save profile…", SaveProfile);
        Button("Reset", () =>
        {
            prefs.Theme = new();
            ApplyProfile();
            Queue(true);
        });
        actions.Controls.Add(UploadButton);
        actions.Controls.Add(ReadButton);
        ReadButton.Click += async (_, _) =>
        {
            if (actionBusy)
                return;
            actionBusy = true;
            UpdateDeviceButtons();
            try
            {
                byte[]? bytes = await this.readTheme();
                if (bytes == null || IsDisposed || Disposing)
                    return;
                var theme = ThemeDocument.Decode(bytes);
                prefs.Theme.Colors = theme.Colors;
                ApplyProfile();
                document = theme;
                rasterNeeded = false;
                Queue(false);
            }
            catch (Exception e)
            {
                MessageBox.Show(this, e.Message, "Device theme");
            }
            finally
            {
                actionBusy = false;
                if (!IsDisposed && !Disposing)
                    UpdateDeviceButtons();
            }
        };
        left.Controls.Add(actions);
        screen.Items.AddRange(["Main Menu", "Settings", "Now Playing", "Sync Mode"]);
        screen.SelectedIndex = 0;
        right.Controls.Add(screen);
        right.Controls.Add(preview);
        right.Controls.Add(new Label { Text = "160 × 128 pixels · RGB565\r\nPreview uses the exported glyph coverage and device blend formula.\r\nSample screens and sample battery level.", AutoSize = true, MaximumSize = new(450, 0) });
        browse.Click += (_, _) =>
        {
            using var dialog = new OpenFileDialog
            {
                Filter = "Font files|*.ttf;*.otf",
                FileName = fontPath.Text
            };
            if (dialog.ShowDialog(this) == DialogResult.OK)
            {
                fontPath.Text = dialog.FileName;
                Queue(true);
            }
        };
        mainSize.ValueChanged += (_, _) => Queue(true);
        smallSize.ValueChanged += (_, _) => Queue(true);
        smoothing.CheckedChanged += (_, _) => Queue(false);
        screen.SelectedIndexChanged += (_, _) => Queue(false);
        UploadButton.Click += async (_, _) =>
        {
            if (actionBusy)
                return;
            actionBusy = true;
            UpdateDeviceButtons();
            try
            {
                await Rebuild();
                if (IsDisposed || Disposing)
                    return;
                await upload(document!.Encode(), smoothing.Checked);
            }
            catch (Exception e)
            {
                MessageBox.Show(this, e.Message, "Theme", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            finally
            {
                actionBusy = false;
                if (!IsDisposed && !Disposing)
                    UpdateDeviceButtons();
            }
        };
        timer.Tick += async (_, _) =>
        {
            timer.Stop();
            try
            {
                await Rebuild();
            }
            catch (Exception e)
            {
                if (!IsDisposed && !Disposing)
                    preview.AccessibleDescription = e.Message;
            }
        };
        ApplyProfile();
        Shown += async (_, _) =>
        {
            try
            {
                await Rebuild();
            }
            catch (Exception e)
            {
                if (!IsDisposed && !Disposing)
                    preview.AccessibleDescription = e.Message;
            }
        };
    }

    public void SetDeviceState(bool uploadEnabled, bool readEnabled, string uploadText)
    {
        canUpload = uploadEnabled;
        canRead = readEnabled;
        UploadButton.Text = uploadText;
        UpdateDeviceButtons();
    }

    void UpdateDeviceButtons()
    {
        UploadButton.Enabled = canUpload && !actionBusy;
        ReadButton.Enabled = canRead && !actionBusy;
    }

    static Color? ParseColor(string text)
    {
        text = text.Trim().TrimStart('#');
        return text.Length == 6 && int.TryParse(text, System.Globalization.NumberStyles.HexNumber, null, out int value) ? Color.FromArgb((value >> 16) & 255, (value >> 8) & 255, value & 255) : null;
    }

    void Queue(bool raster)
    {
        if (loading)
            return;
        generation++;
        rasterNeeded |= raster;
        timer.Stop();
        timer.Start();
    }

    void ApplyProfile()
    {
        // Invalidate a rasterization that was started before an import/profile/read.
        generation++;
        timer.Stop();
        loading = true;
        var p = prefs.Theme;
        fontPath.Text = p.FontPath;
        mainSize.Value = Math.Clamp(p.MainSize, 8, 20);
        smallSize.Value = Math.Clamp(p.SmallSize, 8, 16);
        smoothing.Checked = p.Smoothing;
        for (int i = 0; i < 12; i++)
        {
            ushort value = p.Colors.Length == 12 ? p.Colors[i] : new ThemeDocument().Colors[i];
            int r = (value >> 11) & 31, g = (value >> 5) & 63, b = value & 31;
            colorBoxes[i].Text = $"#{((r << 3) | (r >> 2)):X2}{((g << 2) | (g >> 4)):X2}{((b << 3) | (b >> 2)):X2}";
        }

        loading = false;
        rasterNeeded = true;
    }

    ThemeProfile Profile()
    {
        var colors = new ushort[12];
        for (int i = 0; i < 12; i++)
        {
            var color = ParseColor(colorBoxes[i].Text) ?? throw new InvalidDataException("Invalid HEX color: " + ThemeDocument.ColorNames[i]);
            colors[i] = ThemeDocument.Rgb565(color.R, color.G, color.B);
        }

        return new()
        {
            FontPath = fontPath.Text,
            MainSize = (int)mainSize.Value,
            SmallSize = (int)smallSize.Value,
            Smoothing = smoothing.Checked,
            Colors = colors
        };
    }

    public async Task Rebuild()
    {
        timer.Stop();
        await rebuildGate.WaitAsync();
        try
        {
            while (!IsDisposed && !Disposing)
            {
                int version = generation;
                var profile = Profile();
                if (document == null || rasterNeeded)
                {
                    var next = await rasterize(profile);
                    if (version != generation)
                        continue;
                    if (IsDisposed || Disposing)
                        return;
                    document = next;
                    rasterNeeded = false;
                }

                // A caller waiting for Rebuild receives the current document, not
                // an early return caused by a competing timer or an obsolete job.
                document.Colors = profile.Colors;
                var image = ThemeRasterizer.Preview(document, profile.Smoothing, screen.SelectedIndex);
                var old = preview.Image;
                preview.Image = image;
                old?.Dispose();
                preview.AccessibleDescription = null;
                prefs.Theme = profile;
                prefs.Save();
                return;
            }
        }
        finally
        {
            rebuildGate.Release();
        }
    }

    void Import()
    {
        using var dialog = new OpenFileDialog
        {
            Filter = "MonoPod themes|*.mpt"
        };
        if (dialog.ShowDialog(this) != DialogResult.OK)
            return;
        if (new FileInfo(dialog.FileName).Length > ThemeDocument.MaxPayload + 44)
            throw new InvalidDataException("Theme exceeds firmware limit.");
        var theme = ThemeDocument.Decode(File.ReadAllBytes(dialog.FileName));
        prefs.Theme.Colors = theme.Colors;
        ApplyProfile();
        document = theme;
        rasterNeeded = false;
        Queue(false);
    }

    async void Export()
    {
        try
        {
            await Rebuild();
            if (IsDisposed || Disposing)
                return;
            using var dialog = new SaveFileDialog
            {
                Filter = "MPT2 extended palette|*.mpt|MPT1 legacy palette|*.mpt",
                FileName = "theme.mpt"
            };
            if (dialog.ShowDialog(this) == DialogResult.OK)
                File.WriteAllBytes(dialog.FileName, document!.Encode(dialog.FilterIndex == 1));
        }
        catch (Exception e)
        {
            MessageBox.Show(this, e.Message, "Theme export");
        }
    }

    async void SaveProfile()
    {
        try
        {
            await Rebuild();
            if (IsDisposed || Disposing)
                return;
            using var dialog = new SaveFileDialog
            {
                Filter = "Theme profile|*.json",
                FileName = "theme-profile.json"
            };
            if (dialog.ShowDialog(this) == DialogResult.OK)
            {
                var data = new ProfileFile(prefs.Theme, Convert.ToBase64String(document!.Encode()));
                File.WriteAllText(dialog.FileName, JsonSerializer.Serialize(data, new JsonSerializerOptions { WriteIndented = true }));
            }
        }
        catch (Exception e)
        {
            MessageBox.Show(this, e.Message, "Theme profile", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    void LoadProfile()
    {
        using var dialog = new OpenFileDialog
        {
            Filter = "Theme profile|*.json"
        };
        if (dialog.ShowDialog(this) != DialogResult.OK)
            return;
        if (new FileInfo(dialog.FileName).Length > 524288)
            throw new InvalidDataException("Theme profile exceeds 512 KiB.");
        var file = JsonSerializer.Deserialize<ProfileFile>(File.ReadAllText(dialog.FileName)) ?? throw new InvalidDataException("Invalid profile");
        if (file.Options is null || file.Options.Colors is null || file.Options.Colors.Length != 12)
            throw new InvalidDataException("Invalid profile palette.");
        var imported = file.Theme == null ? null : ThemeDocument.Decode(Convert.FromBase64String(file.Theme));
        prefs.Theme = file.Options;
        ApplyProfile();
        if (imported != null)
        {
            document = imported;
            rasterNeeded = false;
        }

        Queue(file.Theme == null);
    }

    sealed record ProfileFile(ThemeProfile Options, string? Theme);
    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            generation++;
            timer.Dispose();
            preview.Image?.Dispose();
        }

        base.Dispose(disposing);
    }
}

public sealed class PixelPictureBox : PictureBox
{
    protected override void OnPaint(PaintEventArgs e)
    {
        if (Image == null)
        {
            base.OnPaint(e);
            return;
        }

        e.Graphics.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.NearestNeighbor;
        e.Graphics.PixelOffsetMode = System.Drawing.Drawing2D.PixelOffsetMode.Half;
        e.Graphics.DrawImage(Image, ClientRectangle);
    }
}
