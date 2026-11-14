namespace MonoPod.Desktop;
internal static class Program
{
    [STAThread]
    static void Main(string[] args)
    {
        ApplicationConfiguration.Initialize();
        Application.ThreadException += (_, e) => MessageBox.Show(e.Exception.Message, "MonoPod Desktop", MessageBoxButtons.OK, MessageBoxIcon.Error);
        float scale = 1f;
        string? scaleArg = args.FirstOrDefault(x => x.StartsWith("--smoke-scale="));
        if (scaleArg != null)
            float.TryParse(scaleArg.Split('=')[1], System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out scale);
        Application.Run(new Forms.MainForm(args.Contains("--smoke"), scale));
    }
}
