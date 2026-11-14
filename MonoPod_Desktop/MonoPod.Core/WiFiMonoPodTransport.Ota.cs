using System.Diagnostics;
using System.Globalization;
using System.Net;
using System.Net.Http.Headers;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace MonoPod.Core;

public sealed partial class WiFiMonoPodTransport
{
    readonly object ownershipGate = new();
    int normalRequests;
    bool otaOwned;
    public TimeSpan OtaAcknowledgementTimeout { get; init; } = TimeSpan.FromSeconds(90);

    sealed class Lease(Action release) : IDisposable
    {
        public void Dispose() => release();
    }

    IDisposable EnterNormalRequest()
    {
        lock (ownershipGate)
        {
            if (otaOwned) throw new InvalidOperationException("Firmware update owns this connection. Wait for it to finish.");
            normalRequests++;
        }
        return new Lease(() => { lock (ownershipGate) normalRequests--; });
    }

    IDisposable EnterOta()
    {
        lock (ownershipGate)
        {
            if (otaOwned || normalRequests != 0)
                throw new InvalidOperationException("Another device operation is active. Finish or cancel it before firmware update.");
            otaOwned = true;
        }
        return new Lease(() => { lock (ownershipGate) otaOwned = false; });
    }

    public async Task<OtaInfo> GetOtaInfoAsync(CancellationToken ct)
    {
        using var ownership = EnterNormalRequest();
        return await OtaInfoCore(ct);
    }

    async Task<OtaInfo> OtaInfoCore(CancellationToken ct)
    {
        using var deadline = Deadline(ct);
        using var response = await client.GetAsync("api/ota/info", deadline.Token);
        if (response.StatusCode == HttpStatusCode.NotFound)
            throw new NotSupportedException("This firmware does not support OTA. Perform the first update through USB.");
        string body = await Response(response, deadline.Token);
        var current = WireJson.Read<OtaInfo>(body);
        current.Validate();
        return current;
    }

    public async Task<OtaReceipt> UploadFirmwareAsync(string path, OtaInfo expected, string pin,
        IProgress<TransferProgress>? progress, IProgress<FirmwareUpdateState>? state, CancellationToken ct)
    {
        if (pin.Length != 6 || pin.Any(c => c < '0' || c > '9'))
            throw new ArgumentException("Enter exactly six digits from the MonoPod screen.");
        using var ownership = EnterOta();
        state?.Report(FirmwareUpdateState.CheckingDevice);
        var current = await OtaInfoCore(ct);
        current.EnsureSupported();
        if (current.DeviceId != expected.DeviceId || current.Board != expected.Board)
            throw new InvalidDataException("The device ID or board changed. Check the displayed IP before updating.");
        using (var check = Deadline(ct))
        {
            var live = WireJson.Read<DeviceInfo>(await Response(await client.GetAsync("api/info", check.Token), check.Token));
            if (live.SyncMode != "wifi" || live.Firmware != current.Firmware)
                throw new InvalidOperationException("Keep the same MonoPod in Wi-Fi Sync before updating.");
        }
        await using var file = FirmwareImage.Open(path);
        var image = await FirmwareImage.InspectAsync(file, path, current, ct);
        // FileShare.Read keeps the exact original bytes stable through hashing and upload.
        string digest = Convert.ToHexString(await SHA256.HashDataAsync(file, ct)).ToLowerInvariant();
        file.Position = 0;
        long sent = 0;
        bool requestStarted = false, bodySent = false;
        using var deadline = Deadline(ct, transferIdleTimeout);
        using var content = new OtaMultipartContent(file, deadline.Token, p =>
        {
            sent = p.Completed;
            deadline.CancelAfter(transferIdleTimeout);
            progress?.Report(p);
        }, () =>
        {
            bodySent = true;
            deadline.CancelAfter(OtaAcknowledgementTimeout);
            state?.Report(FirmwareUpdateState.WaitingDeviceAck);
        });
        using var request = new HttpRequestMessage(HttpMethod.Post, "api/ota/upload") { Content = content };
        request.Headers.Add("X-MonoPod-OTA-PIN", pin);
        request.Headers.Add("X-MonoPod-OTA-Size", image.Size.ToString(CultureInfo.InvariantCulture));
        request.Headers.Add("X-MonoPod-OTA-SHA256", digest);
        try
        {
            ct.ThrowIfCancellationRequested();
            state?.Report(FirmwareUpdateState.Uploading);
            requestStarted = true;
            using var response = await client.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, deadline.Token);
            string body = await response.Content.ReadAsStringAsync(deadline.Token);
            if (!response.IsSuccessStatusCode)
                throw Rejection(response.StatusCode, body, sent, pin);
            using var json = JsonDocument.Parse(body);
            var root = json.RootElement;
            if (response.StatusCode != HttpStatusCode.OK || root.ValueKind != JsonValueKind.Object ||
                !root.TryGetProperty("ok", out var ok) || ok.ValueKind != JsonValueKind.True ||
                !root.TryGetProperty("status", out var status) || status.GetString() != "verified" ||
                !root.TryGetProperty("rebooting", out var rebooting) || rebooting.ValueKind != JsonValueKind.True || !bodySent)
                throw new InvalidDataException("The device did not confirm a verified image and scheduled reboot.");
            state?.Report(FirmwareUpdateState.Rebooting);
            return new(current.DeviceId, current.Firmware, current.Board, digest, image.Size);
        }
        catch (OtaTransferException) { state?.Report(FirmwareUpdateState.Error); throw; }
        catch (Exception error) when (error is HttpRequestException or IOException or InvalidDataException or OperationCanceledException or JsonException or InvalidOperationException)
        {
            state?.Report(FirmwareUpdateState.Error);
            bool uncertain = requestStarted;
            string reason = ct.IsCancellationRequested ? "The request was cancelled." :
                deadline.IsCancellationRequested ? "The OTA request timed out." : "The OTA connection or acknowledgement failed.";
            throw new OtaTransferException(reason + (uncertain
                ? " Device outcome is unknown. Cancellation does not guarantee rollback. Reopen Wi-Fi Sync and reconnect to check the running version."
                : " No firmware request was sent."), uncertain, sent, error);
        }
    }

    static OtaTransferException Rejection(HttpStatusCode status, string body, long sent, string pin)
    {
        string code = "OTA_REJECTED";
        try
        {
            using var json = JsonDocument.Parse(body);
            if (json.RootElement.ValueKind == JsonValueKind.Object && json.RootElement.TryGetProperty("error", out var error) && error.ValueKind == JsonValueKind.String &&
                Regex.IsMatch(error.GetString()!, @"\A[A-Z0-9_]{1,64}\z") && !error.GetString()!.Contains(pin, StringComparison.Ordinal)) code = error.GetString()!;
        }
        catch (JsonException) { }
        string detail = code switch
        {
            "INVALID_PIN" => "Incorrect screen PIN. Read the current PIN on MonoPod; do not retry automatically.",
            "PIN_LOCKED_RESTART_SYNC" => "Five PIN attempts failed. Exit Wi-Fi Sync on MonoPod and open it again.",
            "NO_OTA_PARTITION" => "Install a dual-OTA partition table once through USB.",
            "TRANSFER_ACTIVE" => "A transfer is already active on MonoPod.",
            "REBOOT_PENDING" => "MonoPod is waiting to reboot. Reconnect afterwards.",
            _ => "MonoPod rejected the firmware image. Check its application format, chip, size and integrity."
        };
        return new OtaTransferException($"HTTP {(int)status}: {code}. {detail}", false, sent);
    }

    sealed class OtaMultipartContent : HttpContent
    {
        readonly Stream input;
        readonly byte[] prefix, suffix;
        readonly CancellationToken ct;
        readonly Action<TransferProgress> progress;
        readonly Action sent;
        public OtaMultipartContent(Stream input, CancellationToken ct, Action<TransferProgress> progress, Action sent)
        {
            this.input = input; this.ct = ct; this.progress = progress; this.sent = sent;
            string boundary = "monopod-ota-" + Guid.NewGuid().ToString("N");
            prefix = Encoding.ASCII.GetBytes($"--{boundary}\r\nContent-Disposition: form-data; name=\"firmware\"; filename=\"firmware.bin\"\r\nContent-Type: application/octet-stream\r\n\r\n");
            suffix = Encoding.ASCII.GetBytes($"\r\n--{boundary}--\r\n");
            Headers.ContentType = new MediaTypeHeaderValue("multipart/form-data");
            Headers.ContentType.Parameters.Add(new NameValueHeaderValue("boundary", boundary));
            Headers.ContentLength = prefix.Length + input.Length + suffix.Length;
        }
        protected override bool TryComputeLength(out long length)
        {
            length = prefix.Length + input.Length + suffix.Length; return true;
        }
        protected override async Task SerializeToStreamAsync(Stream target, TransportContext? context)
        {
            await target.WriteAsync(prefix, ct);
            var watch = Stopwatch.StartNew();
            var buffer = new byte[16384];
            long completed = 0;
            int count;
            while ((count = await input.ReadAsync(buffer, ct)) > 0)
            {
                await target.WriteAsync(buffer.AsMemory(0, count), ct);
                completed += count;
                progress(new("firmware.bin", completed, input.Length, watch.Elapsed));
            }
            await target.WriteAsync(suffix, ct);
            sent();
        }
    }
}
