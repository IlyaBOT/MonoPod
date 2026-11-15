using MonoPod.Core;
using Xunit;

namespace MonoPod.Desktop.Tests;

public sealed class FirmwareImageTests
{
    [Theory]
    [InlineData(4)]
    [InlineData(5)]
    public async Task FastAndSlowReadModesDeclaredByEspIdfAreAccepted(int mode)
    {
        using var image=new FirmwareFixture(flashMode:mode);
        await FirmwareImage.InspectAsync(image.Path,FirmwareFixture.Target,default);
    }
    [Theory]
    [InlineData(true, 9, "esp32s3")]
    [InlineData(false, 9, "esp32s3")]
    [InlineData(true, 0, "esp32")]
    public async Task RealEspLayoutIsRecognizedWithoutInferringVersionFromFilename(bool hash, int chip, string board)
    {
        using var image = new FirmwareFixture(chip: chip, hash: hash, name: "no-version-here.bin");
        var result = await FirmwareImage.InspectAsync(image.Path, FirmwareFixture.Target with { Board = board }, default);
        Assert.Equal(board, result.Board); Assert.Equal(image.Bytes.Length, result.Size);
        Assert.Equal("0.10.7-test", result.ApplicationVersion); Assert.Equal("DIY_iPod_Mono", result.Project);
        Assert.Equal(image.Bytes, await File.ReadAllBytesAsync(image.Path));
    }

    [Theory]
    [InlineData("merged.bin")]
    [InlineData("sketch.ino.merged.bin")]
    [InlineData("bootloader.bin")]
    [InlineData("partitions.bin")]
    [InlineData("boot_app0.bin")]
    [InlineData("application.elf")]
    [InlineData("application.hex")]
    public async Task NonApplicationFileNamesAreRejected(string name)
    {
        using var image = new FirmwareFixture(name: name);
        await Assert.ThrowsAsync<InvalidDataException>(() => FirmwareImage.InspectAsync(image.Path, FirmwareFixture.Target, default));
    }

    [Theory]
    [InlineData("magic")]
    [InlineData("chip")]
    [InlineData("descriptor")]
    [InlineData("checksum")]
    [InlineData("hash")]
    [InlineData("truncated")]
    [InlineData("merged-data")]
    [InlineData("segment-length")]
    [InlineData("segments")]
    [InlineData("flash-mode")]
    [InlineData("short")]
    public async Task CorruptAndIncompatibleImagesAreRejected(string corruption)
    {
        using var image = new FirmwareFixture();
        byte[] bytes = image.Bytes.ToArray();
        switch (corruption)
        {
            case "magic": bytes[0] = 0; break;
            case "chip": bytes[12] = 0; break;
            case "descriptor": bytes[32] = 0; break;
            case "checksum": bytes[^33] ^= 1; break;
            case "hash": bytes[^1] ^= 1; break;
            case "truncated": bytes = bytes[..^1]; break;
            case "merged-data": bytes = [.. bytes, 0xE9, 0, 0, 0]; break;
            case "segment-length": Array.Fill(bytes, (byte)255, 28, 4); break;
            case "segments": bytes[1] = 0; break;
            case "flash-mode": bytes[2] = 6; break;
            case "short": bytes = bytes[..512]; break;
        }
        await File.WriteAllBytesAsync(image.Path, bytes);
        await Assert.ThrowsAsync<InvalidDataException>(() => FirmwareImage.InspectAsync(image.Path, FirmwareFixture.Target, default));
    }

    [Fact]
    public async Task DynamicSlotSizeIsEnforced()
    {
        using var image = new FirmwareFixture();
        await Assert.ThrowsAsync<InvalidDataException>(() => FirmwareImage.InspectAsync(image.Path,
            FirmwareFixture.Target with { MaxImageBytes = image.Bytes.Length - 1 }, default));
    }
}
