using MonoPod.Core;

namespace MonoPod.Desktop.Forms;

public sealed class FirmwareUpdateForm : Form
{
    readonly WiFiMonoPodTransport transport;
    readonly CancellationToken parentToken;
    CancellationTokenSource? request;
    OtaInfo? target;
    FirmwareImageInfo? image;
    bool active, closeRequested;
    readonly Label device = new() { AutoSize = false, Width = 790, Height = 85 };
    readonly TextBox imagePath = new() { Width = 660, ReadOnly = true };
    readonly Label imageDetails = new() { AutoSize = false, Width = 790, Height = 45 };
    readonly Button browse = new() { Text = "Choose application .bin…", AutoSize = true };
    readonly TextBox pin = new() { Width = 140, MaxLength = 6, UseSystemPasswordChar = true };
    readonly CheckBox warning = new()
    {
        AutoSize = false, Width = 790, Height = 54,
        Text = "I confirm: keep power and Wi-Fi connected during update. The battery is charged or stable USB power is connected. Keep MonoPod in Wi-Fi Sync."
    };
    readonly Button upload = new() { Text = "Update firmware…", AutoSize = true };
    readonly Button cancel = new() { Text = "Close", AutoSize = true };
    readonly ProgressBar progress = new() { Width = 790, Height = 20 };
    readonly Label status = new() { AutoSize = false, Width = 790, Height = 100 };
    public FirmwareUpdateState State { get; private set; } = FirmwareUpdateState.Idle;
    public PendingOtaVerification? Pending { get; private set; }
    public string OutcomeMessage { get; private set; } = "Firmware update closed without starting an upload.";
    public event Action<PendingOtaVerification?>? PendingChanged;
    public event Action<FirmwareUpdateState,string>? StateChanged;

    public FirmwareUpdateForm(WiFiMonoPodTransport transport, CancellationToken parentToken)
    {
        this.transport = transport; this.parentToken = parentToken;
        Text = "Firmware Update — MonoPod Wi-Fi OTA";
        ClientSize = new(840, 650);
        MinimumSize = new(860, 690);
        StartPosition = FormStartPosition.CenterParent;
        AutoScaleMode = AutoScaleMode.Dpi;
        MinimizeBox = false;
        var panel = new FlowLayoutPanel
        {
            Dock = DockStyle.Fill, Padding = new(18), FlowDirection = FlowDirection.TopDown,
            WrapContents = false, AutoScroll = true
        };
        panel.Controls.AddRange([
            device, browse, imagePath, imageDetails,
            new Label { Text = "Six-digit OTA PIN shown on the MonoPod display (not the Wi-Fi password)", AutoSize = true }, pin,
            warning,
            new Label { AutoSize = false, Width = 790, Height = 40,
                Text = "Use a trusted local Wi-Fi network. HTTP is unencrypted; SHA-256 verifies integrity, not a firmware signature.\r\nChoose the application/sketch .bin only. Bootloader and partition-table installation requires USB." },
        ]);
        var bottom = new Panel { Dock = DockStyle.Bottom, Height = 180, Padding = new(18,8,18,8) };
        var buttons = new FlowLayoutPanel { Dock = DockStyle.Bottom, Height = 42, FlowDirection = FlowDirection.RightToLeft };
        buttons.Controls.AddRange([cancel,upload]);
        progress.Dock = DockStyle.Top;
        status.Dock = DockStyle.Fill;
        status.Padding = new(0,24,0,0);
        bottom.Controls.Add(status); bottom.Controls.Add(progress); bottom.Controls.Add(buttons);
        Controls.Add(panel);
        Controls.Add(bottom);
        browse.Click += async (_, _) => await ChooseImage();
        upload.Click += async (_, _) =>
        {
            if (!CanUpload()) return;
            if (MessageBox.Show(this, $"Update {target!.DeviceId} ({target.Board})?\r\nCurrent firmware: {target.Firmware}\r\nImage: {Path.GetFileName(image!.Path)} ({DisplayValue.Size(image.Size)})\r\n\r\nDo not disconnect power or Wi-Fi. The uploaded image cannot be cancelled after it is accepted by the device.",
                "Confirm firmware update", MessageBoxButtons.OKCancel, MessageBoxIcon.Warning) != DialogResult.OK) return;
            await SendImage();
        };
        pin.TextChanged += (_, _) => UpdateButtons();
        warning.CheckedChanged += (_, _) => UpdateButtons();
        cancel.Click += (_, _) => { if (active) request?.Cancel(); else Close(); };
        Shown += async (_, _) => await CheckDevice();
        FormClosing += (_, e) =>
        {
            if (active) { e.Cancel = true; closeRequested = true; request?.Cancel(); }
        };
        FormClosed += (_, _) => { pin.Clear(); request?.Dispose(); };
        SetState(FirmwareUpdateState.Idle, "Checking the connected device is required before selecting an image.");
    }

    CancellationToken BeginRequest()
    {
        request?.Dispose();
        request = CancellationTokenSource.CreateLinkedTokenSource(parentToken);
        active = true; UpdateButtons();
        return request.Token;
    }

    void FinishRequest()
    {
        active = false; UpdateButtons();
        if (closeRequested && !IsDisposed) Close();
    }

    bool CanUpload() => !active && State is FirmwareUpdateState.Ready or FirmwareUpdateState.Error &&
        target is { Supported: true, UploadActive: false, RebootPending: false } && image != null && warning.Checked &&
        pin.Text.Length == 6 && pin.Text.All(c => c is >= '0' and <= '9');

    void UpdateButtons()
    {
        browse.Enabled = !active && target is { Supported: true, UploadActive: false, RebootPending: false } && Pending == null;
        pin.Enabled = warning.Enabled = !active && Pending == null;
        upload.Enabled = CanUpload() && Pending == null;
        cancel.Text = active ? State == FirmwareUpdateState.WaitingDeviceAck ? "Stop waiting (outcome unknown)" : "Cancel request" : "Close";
        cancel.Enabled = State != FirmwareUpdateState.Rebooting;
    }

    void SetState(FirmwareUpdateState state, string message)
    {
        if (IsDisposed) return;
        State = state; status.Text = message; UpdateButtons();
        StateChanged?.Invoke(state,message);
    }

    async Task CheckDevice()
    {
        var ct = BeginRequest();
        target = null; image = null;
        imagePath.Clear(); imageDetails.Text = "";
        device.Text = "Connection: " + transport.Description;
        SetState(FirmwareUpdateState.CheckingDevice, "Reading OTA support and current device identity…");
        try
        {
            target = await transport.GetOtaInfoAsync(ct);
            device.Text = $"Connection: {transport.Description}\r\nFirmware: {target.Firmware}    Device ID: {target.DeviceId}\r\nBoard: {target.Board}    OTA: {(target.Supported ? "supported" : "unavailable")}    Slot: {DisplayValue.Size(target.MaxImageBytes)}    PIN: {(target.PinRequired ? "required on device screen" : "not required by API")}";
            target.EnsureSupported();
            SetState(FirmwareUpdateState.SelectingImage, "Choose a local application/sketch .bin. Nothing will be sent until you confirm the update.");
        }
        catch (Exception error)
        {
            OutcomeMessage = error is OperationCanceledException ? "Device check cancelled." : error.Message;
            SetState(FirmwareUpdateState.Error, OutcomeMessage);
        }
        finally { FinishRequest(); }
    }

    async Task ChooseImage()
    {
        if (active || target == null) return;
        using var picker = new OpenFileDialog { Filter = "ESP application/sketch binary (*.bin)|*.bin", Multiselect = false };
        if (picker.ShowDialog(this) != DialogResult.OK) return;
        var ct = BeginRequest();
        image = null; imagePath.Text = picker.FileName;
        imageDetails.Text = "";
        SetState(FirmwareUpdateState.SelectingImage, "Checking image chip, application descriptor, segments and checksum…");
        try
        {
            image = await FirmwareImage.InspectAsync(picker.FileName, target, ct);
            imageDetails.Text = $"Size: {DisplayValue.Size(image.Size)}    Board: {image.Board}\r\nApplication descriptor: {image.Project}, version {image.ApplicationVersion} (not inferred from the filename)";
            SetState(FirmwareUpdateState.Ready, "Image format checked. Enter the screen PIN, read the power warning and confirm Update firmware.");
        }
        catch (Exception error) { SetState(FirmwareUpdateState.Error, error is OperationCanceledException ? "Image selection cancelled." : error.Message); }
        finally { FinishRequest(); }
    }

    async Task SendImage()
    {
        if (target == null || image == null) return;
        var ct = BeginRequest();
        progress.Value = 0;
        bool finished = false;
        try
        {
            var states = new Progress<FirmwareUpdateState>(state =>
            {
                if (finished || IsDisposed) return;
                if (state == FirmwareUpdateState.Uploading && Pending == null)
                {
                    Pending = new() { DeviceId = target.DeviceId, FirmwareBefore = target.Firmware, Board = target.Board,
                        AttemptedAt = DateTimeOffset.UtcNow, AcceptedByDevice = false };
                    PendingChanged?.Invoke(Pending);
                }
                SetState(state, state switch
                {
                    FirmwareUpdateState.CheckingDevice => "Rechecking device identity, Wi-Fi Sync and original image bytes…",
                    FirmwareUpdateState.Uploading => "Sending original image bytes. Transmission progress does not confirm installation.",
                    FirmwareUpdateState.WaitingDeviceAck => "Image bytes sent. Waiting for MonoPod to verify the image and confirm reboot…",
                    FirmwareUpdateState.Rebooting => "Image verified by MonoPod; reboot is scheduled. The new running version is not verified yet.",
                    _ => status.Text
                });
            });
            var transfer = new Progress<TransferProgress>(value =>
            {
                if (IsDisposed) return;
                progress.Value = (int)Math.Clamp(value.Percent, 0, 100);
                if (State == FirmwareUpdateState.Uploading)
                    status.Text = $"Sending image bytes: {value.Percent:F1}% ({DisplayValue.Size(value.Completed)}/{DisplayValue.Size(value.Total)}). Waiting for device acceptance afterwards.";
            });
            var receipt = await transport.UploadFirmwareAsync(image.Path, target, pin.Text, transfer, states, ct);
            finished = true;
            Pending = new() { DeviceId = receipt.DeviceId, FirmwareBefore = receipt.FirmwareBefore, Board = receipt.Board,
                AttemptedAt = DateTimeOffset.UtcNow, AcceptedByDevice = true };
            PendingChanged?.Invoke(Pending);
            OutcomeMessage = "Image verified. MonoPod is rebooting. Open Wi-Fi Sync again on the player, then reconnect to verify the running firmware version.";
            SetState(FirmwareUpdateState.Rebooting, OutcomeMessage);
            await Task.Delay(1900);
            SetState(FirmwareUpdateState.AwaitingUserReconnect, OutcomeMessage);
        }
        catch (Exception error)
        {
            finished = true;
            if (error is not OtaTransferException { OutcomeUncertain: true })
            {
                Pending = null; PendingChanged?.Invoke(null);
            }
            else if (Pending == null)
            {
                Pending = new() { DeviceId = target.DeviceId, FirmwareBefore = target.Firmware, Board = target.Board,
                    AttemptedAt = DateTimeOffset.UtcNow, AcceptedByDevice = false };
                PendingChanged?.Invoke(Pending);
            }
            OutcomeMessage = error is OperationCanceledException ? "Update preparation cancelled. No firmware request was sent." : error.Message;
            SetState(FirmwareUpdateState.Error, OutcomeMessage);
        }
        finally { finished = true; pin.Clear(); FinishRequest(); }
    }
}
