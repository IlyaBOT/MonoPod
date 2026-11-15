using Xunit;
using MonoPod.Core;

namespace MonoPod.Desktop.Tests;
public sealed class UnknownFlacTests
{
    [Fact]
    public async Task ZeroFlacSampleCountMeansUnknownDuration()
    {
        byte[] data = new byte[42];
        "fLaC"u8.CopyTo(data);
        data[4] = 128;
        data[7] = 34;
        data[9] = 16;
        data[11] = 16;
        System.Buffers.Binary.BinaryPrimitives.WriteUInt64BigEndian(data.AsSpan(18, 8), (48000UL << 44) | (1UL << 41) | (15UL << 36));
        var m = await MetadataReader.ReadAsync("stream.flac", data.Length, (offset, count) => Task.FromResult(data.Skip((int)offset).Take(count).ToArray()), default);
        Assert.Equal(48000, m.SampleRate);
        Assert.Null(m.Seconds);
        Assert.Equal("", m.DurationSource);
    }
}
