using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;

namespace MonoPod.Core;

public sealed record FirmwareImageInfo(string Path, long Size, string Board, string ApplicationVersion, string Project);

public static class FirmwareImage
{
    public static async Task<FirmwareImageInfo> InspectAsync(string path, OtaInfo target, CancellationToken ct)
    {
        await using var file = Open(path);
        return await InspectAsync(file, path, target, ct);
    }

    internal static FileStream Open(string path) => new(path, FileMode.Open, FileAccess.Read, FileShare.Read,
        65536, FileOptions.Asynchronous | FileOptions.SequentialScan);

    internal static async Task<FirmwareImageInfo> InspectAsync(FileStream file, string path, OtaInfo target, CancellationToken ct)
    {
        target.EnsureSupported();
        string name = System.IO.Path.GetFileName(path);
        if (!System.IO.Path.GetExtension(path).Equals(".bin", StringComparison.OrdinalIgnoreCase) ||
            Regex.IsMatch(name, @"(?:^|[._-])(?:merged|bootloader|partitions?|boot_app0)(?:[._-]|$)", RegexOptions.IgnoreCase))
            throw new InvalidDataException("Select the application/sketch .bin, not merged, bootloader, partitions, ELF or HEX.");
        if (file.Length < 1024 || file.Length > target.MaxImageBytes)
            throw new InvalidDataException($"Image size {file.Length} bytes does not fit the OTA slot (1024–{target.MaxImageBytes} bytes).");
        file.Position = 0;
        var header = new byte[24];
        await file.ReadExactlyAsync(header, ct);
        if (header[0] != 0xE9 || header[1] is < 1 or > 16 || header[2] > 5 || header[23] > 1)
            throw new InvalidDataException("Invalid ESP application image header.");
        int chip = BinaryPrimitives.ReadUInt16LittleEndian(header.AsSpan(12, 2));
        string board = chip switch { 0 => "esp32", 9 => "esp32s3", _ => "unknown" };
        if (board != target.Board)
            throw new InvalidDataException($"Image chip {board} (ID {chip}) does not match device board {target.Board}.");
        var segment = new byte[8];
        var buffer = new byte[16384];
        byte checksum = 0xEF;
        string version = "", project = "";
        for (int index = 0; index < header[1]; index++)
        {
            if (file.Position + 8 > file.Length) throw new InvalidDataException("Truncated ESP segment header.");
            await file.ReadExactlyAsync(segment, ct);
            long length = BinaryPrimitives.ReadUInt32LittleEndian(segment.AsSpan(4, 4));
            if (length <= 0 || length > file.Length - file.Position || index == 0 && length < 256)
                throw new InvalidDataException("Invalid ESP segment length.");
            bool firstBlock = true;
            while (length > 0)
            {
                ct.ThrowIfCancellationRequested();
                int count = (int)Math.Min(buffer.Length, length);
                await file.ReadExactlyAsync(buffer.AsMemory(0, count), ct);
                if (index == 0 && firstBlock)
                {
                    if (BinaryPrimitives.ReadUInt32LittleEndian(buffer.AsSpan(0, 4)) != 0xABCD5432)
                        throw new InvalidDataException("Missing ESP-IDF application descriptor. This may be a bootloader or merged image.");
                    version = Text(buffer, 16, 32);
                    project = Text(buffer, 48, 32);
                }
                for (int i = 0; i < count; i++) checksum ^= buffer[i];
                length -= count;
                firstBlock = false;
            }
        }
        long checksumEnd = (file.Position + 16) & ~15L;
        long expected = checksumEnd + (header[23] == 1 ? 32 : 0);
        if (expected != file.Length)
            throw new InvalidDataException("Truncated image or extra data after the application. Merged and signed-container images are not accepted.");
        var footer = new byte[(int)(checksumEnd - file.Position)];
        await file.ReadExactlyAsync(footer, ct);
        if (footer[^1] != checksum || footer.Take(footer.Length - 1).Any(b => b != 0))
            throw new InvalidDataException("ESP image checksum or padding is invalid.");
        if (header[23] == 1)
        {
            var expectedHash = new byte[32];
            await file.ReadExactlyAsync(expectedHash, ct);
            file.Position = 0;
            using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
            long left = checksumEnd;
            while (left > 0)
            {
                int count = (int)Math.Min(buffer.Length, left);
                await file.ReadExactlyAsync(buffer.AsMemory(0, count), ct);
                hash.AppendData(buffer, 0, count);
                left -= count;
            }
            if (!CryptographicOperations.FixedTimeEquals(expectedHash, hash.GetHashAndReset()))
                throw new InvalidDataException("The ESP image's appended SHA-256 is invalid.");
        }
        file.Position = 0;
        return new(path, file.Length, board, version, project);
    }

    static string Text(byte[] bytes, int offset, int count)
    {
        int end = Array.IndexOf(bytes, (byte)0, offset, count);
        return Encoding.ASCII.GetString(bytes, offset, (end < 0 ? offset + count : end) - offset)
            .Trim().Replace('\r', ' ').Replace('\n', ' ');
    }
}
