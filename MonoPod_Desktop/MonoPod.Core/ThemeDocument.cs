using System.Buffers.Binary;

namespace MonoPod.Core;
public sealed record ThemeGlyph(uint Codepoint, byte Width, byte Advance, byte[] Pixels);
public sealed class ThemeDocument
{
    public const int MaxPayload = 131072;
    // First four indices deliberately retain the MPT1 order.
    public static readonly string[] ColorNames = ["Background", "Foreground", "Selection Background", "Muted Text", "Selection Foreground", "Icons", "Active Icons", "Accent", "Status Bar Text", "Status Bar Icons", "Progress Bar", "Borders"];
    public ushort[] Colors { get; set; } = [0xce59, 0x2104, 0x2945, 0x8c71, 0xce59, 0x2104, 0x2945, 0x2945, 0x2104, 0x2104, 0x2945, 0x2104];
    public List<ThemeGlyph> Main { get; set; } = [];
    public List<ThemeGlyph> Small { get; set; } = [];

    public static ushort Rgb565(int r, int g, int b) => (ushort)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    public static (int R, int G, int B) Rgb888(ushort c) => (((c >> 11) & 31) * 255 / 31, ((c >> 5) & 63) * 255 / 63, (c & 31) * 255 / 31);
    public byte[] Encode(bool v2 = true)
    {
        using var data = new MemoryStream();
        using (var writer = new BinaryWriter(data, System.Text.Encoding.UTF8, true))
        {
            foreach (var list in new[]
            {
                Main,
                Small
            }

            )
                foreach (var glyph in list.OrderBy(x => x.Codepoint))
                {
                    writer.Write(glyph.Codepoint);
                    writer.Write(glyph.Width);
                    writer.Write(glyph.Advance);
                    writer.Write((byte)0);
                    writer.Write(glyph.Pixels);
                }
        }

        byte[] payload = data.ToArray();
        using var output = new MemoryStream();
        using (var writer = new BinaryWriter(output, System.Text.Encoding.UTF8, true))
        {
            writer.Write(System.Text.Encoding.ASCII.GetBytes(v2 ? "MPT2" : "MPT1"));
            writer.Write((byte)(v2 ? 2 : 1));
            writer.Write((byte)1);
            writer.Write((byte)16);
            writer.Write((byte)12);
            writer.Write((ushort)Main.Count);
            writer.Write((ushort)Small.Count);
            foreach (var color in Colors.Take(v2 ? 12 : 4))
                writer.Write(color);
            writer.Write(payload.Length);
            writer.Write(Crc32.Compute(payload));
            writer.Write(payload);
        }

        byte[] raw = output.ToArray();
        Decode(raw);
        return raw;
    }

    public static ThemeDocument Decode(byte[] raw)
    {
        if (raw.Length < 28)
            throw new InvalidDataException("Truncated theme header.");
        bool v2 = raw.AsSpan(0, 4).SequenceEqual("MPT2"u8) && raw[4] == 2;
        int header = v2 ? 44 : 28;
        if ((!v2 && (!raw.AsSpan(0, 4).SequenceEqual("MPT1"u8) || raw[4] != 1)) || raw[5] != 1 || raw[6] != 16 || raw[7] != 12)
            throw new InvalidDataException("Incompatible theme version or glyph geometry.");
        if (raw.Length < header)
            throw new InvalidDataException("Truncated MPT2 header.");
        uint n = BinaryPrimitives.ReadUInt32LittleEndian(raw.AsSpan(header - 8, 4)), crc = BinaryPrimitives.ReadUInt32LittleEndian(raw.AsSpan(header - 4, 4));
        if (n > MaxPayload || n != raw.Length - header || Crc32.Compute(raw.AsSpan(header)) != crc)
            throw new InvalidDataException("Invalid theme length or CRC32.");
        var doc = new ThemeDocument();
        for (int i = 0; i < (v2 ? 12 : 4); i++)
            doc.Colors[i] = BinaryPrimitives.ReadUInt16LittleEndian(raw.AsSpan(12 + i * 2, 2));
        if (!v2)
        {
            doc.Colors[4] = doc.Colors[0];
            for (int i = 5; i < 12; i++)
                doc.Colors[i] = doc.Colors[1];
        }

        int pos = header;
        for (int table = 0; table < 2; table++)
        {
            int count = BinaryPrimitives.ReadUInt16LittleEndian(raw.AsSpan(8 + table * 2, 2));
            if (count < 1 || count > 256)
                throw new InvalidDataException("Invalid glyph count.");
            uint prior = 0;
            for (int i = 0; i < count; i++)
            {
                if (pos + 7 > raw.Length)
                    throw new InvalidDataException("Truncated glyph record.");
                uint code = BinaryPrimitives.ReadUInt32LittleEndian(raw.AsSpan(pos, 4));
                byte width = raw[pos + 4], advance = raw[pos + 5], reserved = raw[pos + 6];
                pos += 7;
                int bytes = (width * (table == 0 ? 16 : 12) + 1) / 2;
                if (code > 0x10ffff || code is >= 0xd800 and <= 0xdfff || (i > 0 && code <= prior) || width is < 1 or > 24 || advance is < 1 or > 32 || reserved != 0 || pos + bytes > raw.Length)
                    throw new InvalidDataException("Invalid glyph data.");
                (table == 0 ? doc.Main : doc.Small).Add(new(code, width, advance, raw.AsSpan(pos, bytes).ToArray()));
                pos += bytes;
                prior = code;
            }
        }

        if (pos != raw.Length)
            throw new InvalidDataException("Unexpected trailing theme data.");
        return doc;
    }
}
