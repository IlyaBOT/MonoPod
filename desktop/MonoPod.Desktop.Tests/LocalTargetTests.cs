using Xunit;
using MonoPod.Core;

namespace MonoPod.Desktop.Tests;
public sealed class LocalTargetTests
{
    [Theory]
    [InlineData("../escape.mp3")]
    [InlineData("..\\escape.mp3")]
    [InlineData("/absolute.mp3")]
    [InlineData("C:escape.mp3")]
    [InlineData("folder/track.mp3")]
    public void DeviceNamesCannotEscapeDownloadFolder(string name) => Assert.ThrowsAny<Exception>(() => Downloads.LocalTarget(Path.GetTempPath(), name));
    [Fact]
    public void UnicodeDownloadStaysInFolder()
    {
        string dir = Path.Combine(Path.GetTempPath(), "monopod-test");
        Assert.Equal(Path.Combine(dir, "Песня.mp3"), Downloads.LocalTarget(dir, "Песня.mp3"));
    }

    [Fact]
    public void RootDownloadFolderIsSupported()
    {
        string root = Path.GetPathRoot(Path.GetTempPath())!;
        Assert.Equal(Path.Combine(root, "track.mp3"), Downloads.LocalTarget(root, "track.mp3"));
    }
}
