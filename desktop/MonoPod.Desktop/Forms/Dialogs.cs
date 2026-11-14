using MonoPod.Core;

namespace MonoPod.Desktop.Forms;
internal static class Dialogs
{
    public static string? Text(IWin32Window owner, string title, string label, string initial = "")
    {
        using var f = new Form
        {
            Text = title,
            Width = 470,
            Height = 160,
            StartPosition = FormStartPosition.CenterParent,
            FormBorderStyle = FormBorderStyle.FixedDialog,
            MinimizeBox = false,
            MaximizeBox = false,
            AutoScaleMode = AutoScaleMode.Dpi
        };
        var box = new TextBox
        {
            Text = initial,
            Dock = DockStyle.Top
        };
        var ok = new Button
        {
            Text = "OK",
            DialogResult = DialogResult.OK,
            AutoSize = true
        };
        var cancel = new Button
        {
            Text = "Cancel",
            DialogResult = DialogResult.Cancel,
            AutoSize = true
        };
        var buttons = new FlowLayoutPanel
        {
            Dock = DockStyle.Bottom,
            Height = 40,
            FlowDirection = FlowDirection.RightToLeft
        };
        buttons.Controls.AddRange([cancel, ok]);
        f.Controls.Add(box);
        f.Controls.Add(new Label { Text = label, Dock = DockStyle.Top, Height = 25 });
        f.Controls.Add(buttons);
        f.AcceptButton = ok;
        f.CancelButton = cancel;
        return f.ShowDialog(owner) == DialogResult.OK ? box.Text : null;
    }

    public static bool UsbWarning(IWin32Window owner, Services.Preferences prefs)
    {
        if (!prefs.WarnUsb)
            return true;
        using var f = new Form
        {
            Text = "USB Sync may be slow",
            Width = 530,
            Height = 240,
            StartPosition = FormStartPosition.CenterParent,
            AutoScaleMode = AutoScaleMode.Dpi,
            MinimizeBox = false,
            MaximizeBox = false,
            FormBorderStyle = FormBorderStyle.FixedDialog
        };
        var label = new Label
        {
            Text = "MonoPod uses USB CDC for synchronization. Transferring large music libraries over USB may take significantly longer than Wi-Fi.\r\n\r\nDo you want to continue?",
            Dock = DockStyle.Fill,
            Padding = new Padding(16)
        };
        var check = new CheckBox
        {
            Text = "Don't show this warning again",
            AutoSize = true
        };
        var yes = new Button
        {
            Text = "Continue",
            DialogResult = DialogResult.OK,
            AutoSize = true
        };
        var no = new Button
        {
            Text = "Cancel",
            DialogResult = DialogResult.Cancel,
            AutoSize = true
        };
        var row = new FlowLayoutPanel
        {
            Dock = DockStyle.Bottom,
            Height = 65,
            Padding = new Padding(10)
        };
        row.Controls.AddRange([check, yes, no]);
        f.Controls.Add(label);
        f.Controls.Add(row);
        f.AcceptButton = yes;
        f.CancelButton = no;
        bool result = f.ShowDialog(owner) == DialogResult.OK;
        if (result && check.Checked)
        {
            prefs.WarnUsb = false;
            prefs.Save();
        }

        return result;
    }

    public static (string Decision, bool All) Conflict(IWin32Window owner, string name)
    {
        using var f = new Form
        {
            Text = "File already exists",
            Width = 520,
            Height = 170,
            StartPosition = FormStartPosition.CenterParent,
            AutoScaleMode = AutoScaleMode.Dpi
        };
        var check = new CheckBox
        {
            Text = "Apply to all remaining conflicts",
            Dock = DockStyle.Top
        };
        var row = new FlowLayoutPanel
        {
            Dock = DockStyle.Bottom,
            Height = 40
        };
        string answer = "Cancel";
        foreach (string s in new[]
        {
            "Replace",
            "Skip",
            "Cancel"
        }

        )
        {
            var b = new Button
            {
                Text = s,
                AutoSize = true
            };
            b.Click += (_, _) =>
            {
                answer = s;
                f.DialogResult = DialogResult.OK;
            };
            row.Controls.Add(b);
        }

        f.Controls.Add(check);
        f.Controls.Add(new Label { Text = name, Dock = DockStyle.Top, Height = 35 });
        f.Controls.Add(row);
        f.ShowDialog(owner);
        return (answer, check.Checked);
    }
}

public sealed class TransferProgressForm : Form
{
    readonly DataGridView grid = new()
    {
        Dock = DockStyle.Fill,
        ReadOnly = true,
        AllowUserToAddRows = false,
        RowHeadersVisible = false,
        AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill
    };
    public event Action? CancelRequested;
    bool complete;
    public TransferProgressForm(IEnumerable<string> files)
    {
        Text = "Transfer queue";
        Width = 800;
        Height = 330;
        StartPosition = FormStartPosition.CenterParent;
        AutoScaleMode = AutoScaleMode.Dpi;
        foreach (string s in new[]
        {
            "File Name",
            "Size",
            "Status",
            "Progress",
            "Speed",
            "Remaining Time"
        }

        )
            grid.Columns.Add(s, s);
        foreach (string file in files)
            grid.Rows.Add(Path.GetFileName(file), DisplayValue.Size(new FileInfo(file).Length), "Queued", "", "", "");
        var cancel = new Button
        {
            Text = "Cancel safely",
            Dock = DockStyle.Bottom,
            Height = 35
        };
        cancel.Click += (_, _) => CancelRequested?.Invoke();
        Controls.Add(grid);
        Controls.Add(cancel);
        FormClosing += (_, e) =>
        {
            if (!complete)
            {
                e.Cancel = true;
                CancelRequested?.Invoke();
            }
        };
    }

    public void UpdateRow(int row, string status, TransferProgress? p = null)
    {
        if (IsDisposed)
            return;
        grid.Rows[row].Cells[2].Value = status;
        if (p != null)
        {
            grid.Rows[row].Cells[3].Value = $"{p.Percent:F1}%";
            grid.Rows[row].Cells[4].Value = DisplayValue.Size((long)p.BytesPerSecond) + "/s";
            grid.Rows[row].Cells[5].Value = p.Remaining is { } t ? DisplayValue.Duration(t.TotalSeconds) : "—";
        }
    }

    public void Complete()
    {
        complete = true;
        Close();
    }
}
