using System.Text.Json.Serialization;

namespace MonoPod.Core;

public enum FirmwareUpdateState
{
    Idle, CheckingDevice, SelectingImage, Ready, Uploading, WaitingDeviceAck,
    Rebooting, AwaitingUserReconnect, Verified, Error
}

public sealed record OtaInfo
{
    [JsonRequired] public int Protocol { get; init; }
    [JsonRequired] public string Firmware { get; init; } = "";
    [JsonRequired] public string Board { get; init; } = "";
    [JsonRequired] public string DeviceId { get; init; } = "";
    [JsonRequired] public bool Supported { get; init; }
    [JsonRequired] public long MaxImageBytes { get; init; }
    [JsonRequired] public bool PinRequired { get; init; }
    public string PinLocation { get; init; } = "";
    public bool UploadActive { get; init; }
    public int ProgressPercent { get; init; }
    public bool RebootPending { get; init; }

    public void Validate()
    {
        if (Protocol != 1) throw new NotSupportedException($"Unsupported MonoPod OTA protocol {Protocol}.");
        if (Board is not ("esp32" or "esp32s3") || string.IsNullOrWhiteSpace(DeviceId) || DeviceId.Length > 128 ||
            string.IsNullOrWhiteSpace(Firmware) || MaxImageBytes < 0)
            throw new InvalidDataException("Incomplete or invalid MonoPod OTA information.");
    }

    public void EnsureSupported()
    {
        Validate();
        if (!Supported || MaxImageBytes < 1024)
            throw new NotSupportedException("OTA requires a dual-slot partition table (ota_0, ota_1, otadata). Install it once through USB with the correct flash profile.");
        if (UploadActive || RebootPending)
            throw new InvalidOperationException("MonoPod is already updating or waiting to reboot. Wait and reconnect.");
    }
}

public sealed record OtaReceipt(string DeviceId, string FirmwareBefore, string Board, string Sha256, long ImageBytes);

public sealed record PendingOtaVerification
{
    public string DeviceId { get; init; } = "";
    public string FirmwareBefore { get; init; } = "";
    public string Board { get; init; } = "";
    public bool AcceptedByDevice { get; init; }
    public DateTimeOffset AttemptedAt { get; init; }
}

public sealed record OtaReconnectionResult(bool SameDevice, bool VersionChanged, string Message)
{
    public static OtaReconnectionResult Check(PendingOtaVerification before, DeviceInfo device, OtaInfo current)
    {
        current.Validate();
        if (current.DeviceId != before.DeviceId || current.Board != before.Board)
            return new(false, false, "Different device ID or board. The previous firmware update has not been verified.");
        if (current.UploadActive || current.RebootPending || device.Firmware != current.Firmware)
            throw new InvalidOperationException("The device is still updating or its version information is inconsistent. Reconnect after reboot.");
        bool changed = before.FirmwareBefore != device.Firmware;
        return new(true, changed, $"Device ID verified. Firmware: {before.FirmwareBefore} → {device.Firmware}. " +
            (changed ? "The running version was read after reconnection." : "The version is unchanged; installation of this particular image cannot be confirmed."));
    }
}

public sealed class OtaTransferException(string message, bool outcomeUncertain, long bytesSent, Exception? inner = null)
    : IOException(message, inner)
{
    public bool OutcomeUncertain { get; } = outcomeUncertain;
    public long BytesSent { get; } = bytesSent;
}
