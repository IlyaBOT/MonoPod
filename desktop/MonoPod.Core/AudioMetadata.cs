using System.Buffers.Binary;
using System.Text;

namespace MonoPod.Core;
public sealed record AudioMetadata
{
    public string Title { get; set; } = "";
    public string Artist { get; set; } = "";
    public string Album { get; set; } = "";
    public string AlbumArtist { get; set; } = "";
    public string Year { get; set; } = "";
    public string Genre { get; set; } = "";
    public string TrackNumber { get; set; } = "";
    public string Codec { get; set; } = "";
    public double? Seconds { get; set; }
    public string DurationSource { get; set; } = "";
    public int? Bitrate { get; set; }
    public int? SampleRate { get; set; }
    public string Note { get; set; } = "";
    public string Duration => DisplayValue.Duration(Seconds);
}

// Bounded seek/read parser shared by local files and remote range reads. No filename fallbacks.
public static class MetadataReader
{
    public static async Task<AudioMetadata> ReadAsync(string name, long length, Func<long, int, Task<byte[]>> read, CancellationToken ct)
    {
        var m = new AudioMetadata();
        long budget = 262144;
        async Task<byte[]> R(long offset, int count)
        {
            ct.ThrowIfCancellationRequested();
            if (offset < 0 || offset >= length)
                return[];
            count = (int)Math.Min(count, length - offset);
            if (count > budget)
                throw new InvalidDataException("Metadata read budget exceeded (256 KiB).");
            budget -= count;
            return await read(offset, count);
        }

        static uint BE(byte[] b, int i) => BinaryPrimitives.ReadUInt32BigEndian(b.AsSpan(i, 4));
        static uint LE(byte[] b, int i) => BinaryPrimitives.ReadUInt32LittleEndian(b.AsSpan(i, 4));
        static int Syn(byte[] b, int i) => ((b[i] & 127) << 21) | ((b[i + 1] & 127) << 14) | ((b[i + 2] & 127) << 7) | (b[i + 3] & 127);
        static string Text(byte[] b)
        {
            if (b.Length < 2)
                return "";
            var enc = b[0] switch
            {
                0 => Encoding.Latin1,
                1 => b.Length >= 3 && b[1] == 0xfe && b[2] == 0xff ? Encoding.BigEndianUnicode : Encoding.Unicode,
                2 => Encoding.BigEndianUnicode,
                3 => Encoding.UTF8,
                _ => null
            };
            return enc?.GetString(b, 1, b.Length - 1).Trim('\0', '\ufeff', ' ', '\r', '\n') ?? "";
        }

        try
        {
            string ext = Path.GetExtension(name).ToLowerInvariant();
            var head = await R(0, 12);
            long audioStart = 0;
            if (head.Length >= 10 && Encoding.ASCII.GetString(head, 0, 3) == "ID3" && (head[3] == 3 || head[3] == 4))
            {
                int version = head[3], tagSize = Syn(head, 6);
                audioStart = 10L + tagSize + ((version == 4 && (head[5] & 16) != 0) ? 10 : 0);
                long end = Math.Min(length, 10L + tagSize), pos = 10;
                if ((head[5] & 0x80) == 0)
                { // Unsynchronised tags are left blank, not misdecoded.
                    if ((head[5] & 0x40) != 0)
                    {
                        var x = await R(pos, 4);
                        if (x.Length == 4)
                            pos += version == 4 ? Syn(x, 0) : 4 + BE(x, 0);
                    }

                    for (int frames = 0; frames < 512 && pos + 10 <= end; frames++)
                    {
                        var frame = await R(pos, 10);
                        if (frame.Length != 10 || frame[0] == 0)
                            break;
                        string id = Encoding.ASCII.GetString(frame, 0, 4);
                        uint size = version == 4 ? (uint)Syn(frame, 4) : BE(frame, 4);
                        pos += 10;
                        if (size == 0 || size > end - pos)
                            break;
                        if (frame[9] == 0 && size <= 4096 && id is "TIT2" or "TPE1" or "TALB" or "TPE2" or "TYER" or "TDRC" or "TCON" or "TRCK" or "TLEN")
                        {
                            string text = Text(await R(pos, (int)size));
                            switch (id)
                            {
                                case "TIT2":
                                    m.Title = text;
                                    break;
                                case "TPE1":
                                    m.Artist = text;
                                    break;
                                case "TALB":
                                    m.Album = text;
                                    break;
                                case "TPE2":
                                    m.AlbumArtist = text;
                                    break;
                                case "TYER":
                                case "TDRC":
                                    m.Year = text;
                                    break;
                                case "TCON":
                                    m.Genre = text;
                                    break;
                                case "TRCK":
                                    m.TrackNumber = text;
                                    break;
                                case "TLEN":
                                    if (double.TryParse(text, System.Globalization.NumberStyles.None, System.Globalization.CultureInfo.InvariantCulture, out var ms) && ms > 0 && ms < 864000000)
                                    {
                                        m.Seconds = ms / 1000;
                                        m.DurationSource = "ID3 TLEN";
                                    }

                                    break;
                            }
                        }

                        pos += size;
                    }
                }
                else
                    m.Note = "Unsynchronised ID3v2 tag is not decoded.";
            }

            if (ext == ".wav" && head.Length == 12 && Encoding.ASCII.GetString(head, 0, 4) == "RIFF" && Encoding.ASCII.GetString(head, 8, 4) == "WAVE")
            {
                m.Codec = "WAV";
                long pos = 12;
                uint rate = 0, bytesPerSecond = 0;
                for (int i = 0; i < 256 && pos + 8 <= length; i++)
                {
                    var chunk = await R(pos, 8);
                    if (chunk.Length < 8)
                        break;
                    uint n = LE(chunk, 4);
                    string id = Encoding.ASCII.GetString(chunk, 0, 4);
                    pos += 8;
                    if (n > length - pos)
                        break;
                    if (id == "fmt " && n >= 16)
                    {
                        var fmt = await R(pos, 16);
                        ushort format = BinaryPrimitives.ReadUInt16LittleEndian(fmt);
                        rate = LE(fmt, 4);
                        bytesPerSecond = LE(fmt, 8);
                        m.SampleRate = (int)rate;
                        m.Bitrate = (int)(bytesPerSecond * 8 / 1000);
                        m.Codec = format == 1 ? "PCM WAV" : format == 3 ? "Float WAV" : "WAV (codec " + format + ")";
                    }

                    if (id == "data" && bytesPerSecond > 0)
                    {
                        m.Seconds = n / (double)bytesPerSecond;
                        m.DurationSource = "WAV data / byte rate";
                        break;
                    }

                    pos += n + (n & 1);
                }
            }
            else if (ext == ".flac")
            {
                var sig = await R(audioStart, 4);
                if (Encoding.ASCII.GetString(sig) != "fLaC")
                    return m;
                m.Codec = "FLAC";
                long pos = audioStart + 4;
                for (int i = 0; i < 128 && pos + 4 <= length; i++)
                {
                    var h = await R(pos, 4);
                    int kind = h[0] & 127, n = (h[1] << 16) | (h[2] << 8) | h[3];
                    pos += 4;
                    if (n > length - pos)
                        break;
                    if (kind == 0 && n == 34)
                    {
                        var b = await R(pos, 34);
                        ulong packed = BinaryPrimitives.ReadUInt64BigEndian(b.AsSpan(10, 8));
                        int rate = (int)(packed >> 44);
                        ulong samples = packed & 0xfffffffff;
                        if (rate > 0)
                        {
                            m.SampleRate = rate;
                            if (samples > 0)
                            {
                                m.Seconds = samples / (double)rate;
                                m.DurationSource = "FLAC STREAMINFO sample count";
                            }
                        }
                    }

                    if (kind == 4 && n <= 65536)
                    {
                        var b = await R(pos, n);
                        int at = 0;
                        uint ReadNum()
                        {
                            if (at + 4 > b.Length)
                                throw new InvalidDataException("Truncated Vorbis comment");
                            uint v = LE(b, at);
                            at += 4;
                            return v;
                        }

                        uint vendor = ReadNum();
                        if (vendor > (uint)(b.Length - at))
                            break;
                        at += (int)vendor;
                        uint count = ReadNum();
                        for (int j = 0; j < Math.Min(count, 1024) && at + 4 <= b.Length; j++)
                        {
                            uint len = ReadNum();
                            if (len > (uint)(b.Length - at))
                                break;
                            string text = Encoding.UTF8.GetString(b, at, (int)len);
                            at += (int)len;
                            int equal = text.IndexOf('=');
                            if (equal < 0)
                                continue;
                            string key = text[..equal].ToUpperInvariant(), v = text[(equal + 1)..];
                            switch (key)
                            {
                                case "TITLE":
                                    m.Title = v;
                                    break;
                                case "ARTIST":
                                    m.Artist = v;
                                    break;
                                case "ALBUM":
                                    m.Album = v;
                                    break;
                                case "ALBUMARTIST":
                                    m.AlbumArtist = v;
                                    break;
                                case "DATE":
                                    m.Year = v;
                                    break;
                                case "GENRE":
                                    m.Genre = v;
                                    break;
                                case "TRACKNUMBER":
                                    m.TrackNumber = v;
                                    break;
                            }
                        }
                    }

                    pos += n;
                    if ((h[0] & 128) != 0)
                        break;
                }
            }
            else if (ext == ".mp3")
            {
                m.Codec = "MP3";
                var b = await R(audioStart, 4096);
                for (int i = 0; i + 4 < b.Length; i++)
                {
                    if (b[i] != 255 || (b[i + 1] & 224) != 224)
                        continue;
                    int version = (b[i + 1] >> 3) & 3, layer = (b[i + 1] >> 1) & 3, bi = b[i + 2] >> 4, si = (b[i + 2] >> 2) & 3;
                    if (version == 1 || layer != 1 || bi is 0 or 15 || si == 3)
                        continue;
                    int[] high = [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320], low = [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160], rates = [44100, 48000, 32000];
                    int sr = rates[si] >> (version == 3 ? 0 : version == 2 ? 1 : 2);
                    m.SampleRate = sr;
                    m.Bitrate = version == 3 ? high[bi] : low[bi];
                    int side = version == 3 ? ((b[i + 3] >> 6) == 3 ? 17 : 32) : ((b[i + 3] >> 6) == 3 ? 9 : 17);
                    int at = i + 4 + ((b[i + 1] & 1) != 0 ? 0 : 2) + side;
                    if (at + 12 <= b.Length && (Encoding.ASCII.GetString(b, at, 4)is "Xing" or "Info") && (BE(b, at + 4) & 1) != 0)
                    {
                        uint frames = BE(b, at + 8);
                        if (frames > 0)
                        {
                            m.Seconds = frames * (version == 3 ? 1152d : 576d) / sr;
                            m.DurationSource = "MPEG Xing frame count";
                        }
                    }

                    int vb = i + 36;
                    if (m.Seconds == null && vb + 18 <= b.Length && Encoding.ASCII.GetString(b, vb, 4) == "VBRI")
                    {
                        m.Seconds = BE(b, vb + 14) * 1152d / sr;
                        m.DurationSource = "MPEG VBRI frame count";
                    }

                    break;
                }

                if (string.IsNullOrEmpty(m.Title) && length >= 128)
                {
                    var tail = await R(length - 128, 128);
                    if (Encoding.ASCII.GetString(tail, 0, 3) == "TAG")
                    {
                        string S(int at, int n) => Encoding.Latin1.GetString(tail, at, n).Trim('\0', ' ');
                        m.Title = S(3, 30);
                        if (string.IsNullOrEmpty(m.Artist))
                            m.Artist = S(33, 30);
                        if (string.IsNullOrEmpty(m.Album))
                            m.Album = S(63, 30);
                        if (string.IsNullOrEmpty(m.Year))
                            m.Year = S(93, 4);
                        if (tail[125] == 0 && tail[126] != 0)
                            m.TrackNumber = tail[126].ToString();
                    }
                }
            }
            else if (ext == ".aac")
            {
                m.Codec = "AAC (ADTS)";
                var b = await R(audioStart, 16);
                if (b.Length >= 7 && b[0] == 255 && (b[1] & 246) == 240)
                {
                    int index = (b[2] >> 2) & 15;
                    int[] rates = [96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350];
                    if (index < rates.Length)
                        m.SampleRate = rates[index];
                }
            }
        }
        catch (InvalidDataException e)
        {
            m.Note = e.Message;
        }
        catch (ArgumentException e)
        {
            m.Note = "Invalid metadata: " + e.Message;
        }

        return m;
    }

    public static async Task<AudioMetadata> LocalAsync(string path, CancellationToken ct)
    {
        await using var stream = File.OpenRead(path);
        return await ReadAsync(path, stream.Length, async (offset, count) =>
        {
            stream.Position = offset;
            byte[] b = new byte[count];
            int n = await stream.ReadAtLeastAsync(b, count, false, ct);
            return b[..n];
        }, ct);
    }
}
