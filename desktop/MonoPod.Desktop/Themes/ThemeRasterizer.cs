using MonoPod.Core;
using System.Drawing.Text;

namespace MonoPod.Desktop.Themes;
public sealed class ThemeProfile
{
    public string FontPath { get; set; } = File.Exists(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Fonts), "arialbd.ttf")) ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Fonts), "arialbd.ttf") : "";
    public int MainSize { get; set; } = 15;
    public int SmallSize { get; set; } = 11;
    public bool Smoothing { get; set; }
    public ushort[] Colors { get; set; } = new ThemeDocument().Colors;
}

public static class ThemeRasterizer
{
    public static ThemeDocument Create(ThemeProfile profile)
    {
        using var collection = new PrivateFontCollection();
        if (!string.IsNullOrWhiteSpace(profile.FontPath))
            collection.AddFontFile(profile.FontPath);
        FontFamily family = collection.Families.Length > 0 ? collection.Families[0] : FontFamily.GenericSansSerif;
        FontStyle style = family.IsStyleAvailable(FontStyle.Bold) ? FontStyle.Bold : FontStyle.Regular;
        var doc = new ThemeDocument
        {
            Colors = (ushort[])profile.Colors.Clone()
        };
        foreach (var pair in new[]
        {
            (profile.MainSize, 16, doc.Main),
            (profile.SmallSize, 12, doc.Small)
        }

        )
        {
            using var font = new Font(family, pair.Item1, style, GraphicsUnit.Pixel);
            using var measure = new Bitmap(64, 32);
            using var g = Graphics.FromImage(measure);
            using var format = (StringFormat)StringFormat.GenericTypographic.Clone();
            format.FormatFlags |= StringFormatFlags.MeasureTrailingSpaces | StringFormatFlags.NoWrap;
            int baseline = pair.Item2 == 16 ? 12 : 9;
            float ascent = family.GetCellAscent(style) * font.Size / family.GetEmHeight(style);
            var chars = Enumerable.Range(32, 95).Concat(Enumerable.Range(0x410, 64)).Concat(new[] { 0x401, 0x451, 0x2116 }).Distinct().Order();
            foreach (int cp in chars)
            {
                string text = char.ConvertFromUtf32(cp);
                int advance = Math.Clamp((int)Math.Round(g.MeasureString(text, font, 64, format).Width) + 1, 1, 32);
                int width = Math.Clamp(advance, 1, 24);
                using var image = new Bitmap(width * 4, pair.Item2 * 4);
                using (var canvas = Graphics.FromImage(image))
                {
                    canvas.Clear(Color.Black);
                    canvas.ScaleTransform(4, 4);
                    canvas.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
                    canvas.DrawString(text, font, Brushes.White, new PointF(0, baseline - ascent), format);
                }

                var pixels = new byte[(width * pair.Item2 + 1) / 2];
                for (int y = 0; y < pair.Item2; y++)
                    for (int x = 0; x < width; x++)
                    {
                        int i = y * width + x;
                        int sum = 0;
                        for (int sy = 0; sy < 4; sy++)
                            for (int sx = 0; sx < 4; sx++)
                                sum += image.GetPixel(x * 4 + sx, y * 4 + sy).R;
                        int alpha = Math.Clamp((int)Math.Round(sum / (16d * 17)), 0, 15);
                        if ((i & 1) == 0)
                            pixels[i / 2] = (byte)(alpha << 4);
                        else
                            pixels[i / 2] |= (byte)alpha;
                    }

                pair.Item3.Add(new((uint)cp, (byte)width, (byte)advance, pixels));
            }
        }

        doc.Encode();
        return doc;
    }

    public static Color Color565(ushort c)
    {
        var(r, g, b) = ThemeDocument.Rgb888(c);
        return Color.FromArgb(r, g, b);
    }

    static ushort Blend(ushort back, ushort front, int a)
    {
        int r = (((back >> 11) & 31) * (15 - a) + ((front >> 11) & 31) * a + 7) / 15, g = (((back >> 5) & 63) * (15 - a) + ((front >> 5) & 63) * a + 7) / 15, b = ((back & 31) * (15 - a) + (front & 31) * a + 7) / 15;
        return (ushort)((r << 11) | (g << 5) | b);
    }

    public static Bitmap Preview(ThemeDocument doc, bool smooth, int screen)
    {
        var bitmap = new Bitmap(160, 128);
        var c = doc.Colors;
        using var g = Graphics.FromImage(bitmap);
        g.Clear(Color565(c[0]));
        void Fill(int x, int y, int w, int h, int i)
        {
            using var brush = new SolidBrush(Color565(c[i]));
            g.FillRectangle(brush, x, y, w, h);
        }

        void Line(int x, int y, int w, int h, int i)
        {
            using var pen = new Pen(Color565(c[i]));
            g.DrawRectangle(pen, x, y, w, h);
        }

        void Text(int x, int y, string text, int front, bool small = false, int back = 0)
        {
            var glyphs = small ? doc.Small : doc.Main;
            foreach (var rune in text.EnumerateRunes())
            {
                var ch = glyphs.FirstOrDefault(a => a.Codepoint == rune.Value) ?? glyphs.FirstOrDefault(a => a.Codepoint == '?');
                if (ch == null)
                    continue;
                if (x + ch.Advance > 160)
                    break;
                for (int yy = 0; yy < (small ? 12 : 16) && y + yy < 128; yy++)
                    for (int xx = 0; xx < ch.Advance && x + xx < 160; xx++)
                    {
                        int n = yy * ch.Width + xx;
                        int alpha = xx < ch.Width ? ((n & 1) == 0 ? ch.Pixels[n / 2] >> 4 : ch.Pixels[n / 2] & 15) : 0;
                        if (!smooth)
                            alpha = alpha >= 8 ? 15 : 0;
                        if (x + xx >= 0 && y + yy >= 0)
                            bitmap.SetPixel(x + xx, y + yy, Color565(Blend(c[back], c[front], alpha)));
                    }

                x += ch.Advance;
            }
        }

        if (screen == 3)
        {
            Fill(80, 13, 1, 29, 5);
            Fill(65, 24, 16, 1, 5);
            Fill(65, 17, 1, 8, 5);
            Fill(62, 12, 7, 7, 5);
            Fill(80, 32, 16, 1, 5);
            Fill(95, 23, 1, 10, 5);
            for (int y = -4; y <= 4; y++)
                for (int x = -4; x <= 4; x++)
                    if (x * x + y * y <= 16)
                        Fill(95 + x, 22 + y, 1, 1, 5);
            for (int y = -3; y <= 3; y++)
                for (int x = -3; x <= 3; x++)
                    if (x * x + y * y <= 9)
                        Fill(80 + x, 41 + y, 1, 1, 5);
            for (int y = 5; y <= 14; y++)
            {
                int half = (y - 5) * 5 / 9;
                Fill(80 - half, y, half * 2 + 1, 1, 5);
            }

            // The firmware uses Adafruit's fixed 5x7 font at size 3 for SYNC, independent of the selected theme font.
            byte[][] fixedGlyphs = [[0x46, 0x49, 0x49, 0x49, 0x31], [0x07, 0x08, 0x70, 0x08, 0x07], [0x7f, 0x04, 0x08, 0x10, 0x7f], [0x3e, 0x41, 0x41, 0x41, 0x22]];
            for (int ch = 0; ch < 4; ch++)
                for (int x = 0; x < 5; x++)
                    for (int y = 0; y < 7; y++)
                        if ((fixedGlyphs[ch][x] & (1 << y)) != 0)
                            Fill(44 + ch * 18 + x * 3, 49 + y * 3, 4, 3, 7);
            Text(3, 77, "USB sync active", 1, true);
            Text(3, 101, "DO NOT REMOVE SD", 1, true);
            Text(3, 113, "DO NOT UNPLUG USB", 1, true);
            return bitmap;
        }

        string title = screen == 0 ? "iPod" : screen == 1 ? "Settings" : "Now Playing";
        int titleWidth = title.Sum(ch => doc.Main.FirstOrDefault(a => a.Codepoint == ch)?.Advance ?? 0);
        Text((160 - titleWidth) / 2, 3, title, 8);
        Line(136, 6, 18, 8, 9);
        Fill(155, 9, 2, 3, 9);
        Fill(138, 8, 10, 5, 9);
        Fill(0, 19, 160, 1, 11);
        if (screen == 2)
        {
            Text(7, 21, "1 of 12", 1, true);
            Text(14, 38, "MonoPod", 1);
            Text(14, 56, "Исполнитель", 1);
            Text(14, 74, "Album", 3);
            Line(13, 96, 133, 9, 11);
            Fill(15, 98, 64, 6, 10);
            Text(6, 112, "01:23", 1, true);
            Text(118, 112, "-02:17", 1, true);
            return bitmap;
        }

        string[] labels = screen == 0 ? ["Music", "Extras", "Settings", "Shuffle Songs", "Backlight"] : ["About", "Shuffle", "Repeat", "Clicker", "Backlight"];
        for (int i = 0; i < 5; i++)
        {
            int y = 21 + 18 * i;
            if (i == 0)
                Fill(2, y, 156, 18, 2);
            Text(6, y + 1, labels[i], i == 0 ? 4 : 1, false, i == 0 ? 2 : 0);
            if (i < 3)
            {
                using var brush = new SolidBrush(Color565(c[i == 0 ? 6 : 5]));
                g.FillPolygon(brush, new Point[] { new(150, y + 6), new(155, y + 9), new(150, y + 12) });
            }
        }

        return bitmap;
    }
}
