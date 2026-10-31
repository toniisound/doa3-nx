// SPDX-License-Identifier: GPL-3.0-or-later
// Windows launcher, built twice by tools/package_test.py with the C# compiler
// every Windows ships (.NET Framework 4):
//   Launcher.exe builds DOA3 from the player's disc, picks resolution,
//     anti-aliasing, widescreen, volume, the free camera and the DOAXBV swimsuit switch,
//     remembers them in private\launcher.json, then plays. An ISO, disc folder
//     or default.xbe dropped on it opens the build page with that disc chosen.
//   DOA3.exe (PLAY_ONLY) plays with the saved settings.
// The window draws a menu over a generated beach (tools/launcher-background.jpg); no retail art is used.
// Keep to C# 5: the in-box compiler knows nothing newer.
using System;
using System.Collections.Generic;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Text;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Web.Script.Serialization;
using System.Windows.Forms;

static class Game
{
    public static readonly string Root = AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\');
    public static readonly string Runner = Path.Combine(Root, @"build\recomp-program\Release\recomp_program_runner.exe");
    public static string Private(string name) { return Path.Combine(Root, "private", name); }

    static Dictionary<string, object> ReadJson(string path)
    {
        try { return new JavaScriptSerializer().Deserialize<Dictionary<string, object>>(File.ReadAllText(path)) ?? new Dictionary<string, object>(); }
        catch (Exception) { return new Dictionary<string, object>(); }
    }

    public static Dictionary<string, object> LoadSettings() { return ReadJson(Private("launcher.json")); }

    // The build records the installed version, its play folder and XBE name in
    // private\build.json; a build older than that is USA 3.0 in play-disc.
    static Dictionary<string, object> BuildRecord() { return ReadJson(Private("build.json")); }
    public static string InstalledVersion() { return File.Exists(Runner) ? Str(BuildRecord(), "version", "USA 3.0") : null; }

    public static void SaveSettings(Dictionary<string, object> settings)
    {
        Directory.CreateDirectory(Path.Combine(Root, "private"));
        // UTF-8 without a byte order mark keeps a non-ASCII disc path intact.
        File.WriteAllText(Private("launcher.json"), new JavaScriptSerializer().Serialize(settings));
    }

    public static int Int(Dictionary<string, object> s, string key, int fallback)
    {
        object v;
        try { return s.TryGetValue(key, out v) && v != null ? Convert.ToInt32(v, CultureInfo.InvariantCulture) : fallback; }
        catch (Exception) { return fallback; }
    }

    public static bool Bool(Dictionary<string, object> s, string key, bool fallback)
    {
        object v;
        return s.TryGetValue(key, out v) && v is bool ? (bool)v : fallback;
    }

    public static string Str(Dictionary<string, object> s, string key, string fallback)
    {
        object v;
        return s.TryGetValue(key, out v) && v is string ? (string)v : fallback;
    }

    public static string FirstLine(string path)
    {
        if (!File.Exists(path)) return null;
        string line = File.ReadLines(path).FirstOrDefault();
        return line == null ? null : line.Trim().TrimStart('\uFEFF');
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool CreateHardLink(string link, string target, IntPtr security);

    // A hard-linked view of the extracted disc keeps the game's cache and saves
    // (.recomp-storage) out of the source disc. The first boots fill the cache.
    // The build records the disc it built from; copies cover other drives.
    static string PlayDisc(string folder)
    {
        string disc = Private(folder);
        if (Directory.Exists(disc)) return disc;
        string source = FirstLine(Private("disc-path.txt")) ?? Private(@"imported-disc\disc");
        // Keep one trailing separator so a drive root (a mounted disc) stays D:\ rather than D:.
        source = Path.GetFullPath(source).TrimEnd('\\') + "\\";
        if (!Directory.Exists(source)) throw new IOException("The extracted disc is missing: " + source + ". Build the game again.");
        // Fill a staging folder so an interrupted copy is never taken as complete.
        string staging = disc + ".tmp";
        if (Directory.Exists(staging)) Directory.Delete(staging, true);
        foreach (string file in Directory.EnumerateFiles(source, "*", SearchOption.AllDirectories))
        {
            string relative = file.Substring(source.Length);
            if (relative.StartsWith(@".recomp-storage\", StringComparison.OrdinalIgnoreCase)) continue;
            string target = Path.Combine(staging, relative);
            Directory.CreateDirectory(Path.GetDirectoryName(target));
            if (!CreateHardLink(target, file, IntPtr.Zero)) File.Copy(file, target);
        }
        Directory.Move(staging, disc);
        return disc;
    }

    public static bool IsDoaxbvImage(string path)
    {
        string name = Path.GetFileName(path).ToLowerInvariant();
        return name.EndsWith(".iso") && (name.Contains("xtreme") || name.Contains("doax") || name.Contains("beach"));
    }
    public static bool IsDoaxbvFolder(string path)
    {
        return File.Exists(Path.Combine(path, "datahdd.afs")) && File.Exists(Path.Combine(path, "datadvd.afs"));
    }

    // None chosen: a DOAXBV .iso or extracted folder (datahdd.afs and datadvd.afs) in private\,
    // or the disc of a doaxbv-re checkout beside this one.
    static string FindDoaxbv()
    {
        string priv = Path.Combine(Root, "private");
        string sister = Path.Combine(Path.GetDirectoryName(Root) ?? Root, @"doaxbv-re\private\disc");
        var folders = new List<string>();
        if (Directory.Exists(priv))
        {
            string image = Directory.EnumerateFiles(priv, "*.iso").FirstOrDefault(IsDoaxbvImage);
            if (image != null) return image;
            foreach (string d in Directory.GetDirectories(priv)) { folders.Add(d); folders.AddRange(Directory.GetDirectories(d)); }
        }
        if (Directory.Exists(sister)) { folders.Add(sister); folders.AddRange(Directory.GetDirectories(sister)); }
        return folders.FirstOrDefault(IsDoaxbvFolder);
    }

    // The swimsuit switch: DOAXBV swimsuits built from the player's DOAXBV disc
    // (an .iso or its extracted folder), remembered in private\doaxbv-path.txt.
    static void SwimsuitEnv(StringDictionary env, Dictionary<string, object> settings)
    {
        if (string.IsNullOrEmpty(env["RECOMP_DOAXBV_DISC"]))
        {
            string chosen = FirstLine(Private("doaxbv-path.txt"));
            if (chosen == null || !(File.Exists(chosen) || Directory.Exists(chosen)))
            {
                chosen = FindDoaxbv();
                if (chosen == null) return;
                File.WriteAllText(Private("doaxbv-path.txt"), chosen);
            }
            env["RECOMP_DOAXBV_DISC"] = chosen;
        }
        string packed = Path.Combine(Root, @"recipes\doaxbv-suits.pack.gz");
        if (!File.Exists(packed)) packed = Private("doaxbv-suits.pack.gz");
        if (!File.Exists(packed)) return;
        // The runtime reads the suit recipes unpacked, so unpack once into private\.
        string pack = Private("doaxbv-suits.pack");
        if (!File.Exists(pack) || File.GetLastWriteTimeUtc(pack) < File.GetLastWriteTimeUtc(packed))
        {
            using (var input = new GZipStream(File.OpenRead(packed), CompressionMode.Decompress))
            using (var output = File.Create(pack + ".tmp"))
                input.CopyTo(output);
            File.Delete(pack);
            File.Move(pack + ".tmp", pack);
        }
        env["RECOMP_COSTUME_PACK"] = pack;
        if (Bool(settings, "swimsuit_start_favorite", false)) env["RECOMP_COSTUME_START_FAVORITE"] = "1";
    }

    // Starts the runner with these settings; returns why it failed, or null.
    public static string Play(Dictionary<string, object> settings)
    {
        if (!File.Exists(Runner)) return "The game is not built yet. Build it in Launcher.exe.";
        var record = BuildRecord();
        string disc = PlayDisc(Str(record, "play_disc", "play-disc"));
        var start = new ProcessStartInfo("cmd.exe") { UseShellExecute = false, CreateNoWindow = true, WorkingDirectory = Root };
        var env = start.EnvironmentVariables;
        // The runner parses these with atof, so always write a '.' decimal point.
        env["RECOMP_D3D_SCALE"] = (Int(settings, "height", 480) / 480.0).ToString(CultureInfo.InvariantCulture);
        env["RECOMP_D3D_MSAA"] = Int(settings, "msaa", 1).ToString(CultureInfo.InvariantCulture);
        env["RECOMP_D3D_SMAA"] = Bool(settings, "smaa", false) ? "1" : "0";
        env["RECOMP_D3D_WIDESCREEN"] = Bool(settings, "widescreen", false) ? "1" : "0";
        env["RECOMP_FREE_CAMERA"] = Bool(settings, "free_camera", false) ? "1" : "0";
        env["RECOMP_AUDIO_GAIN"] = (Int(settings, "volume", 100) / 100.0).ToString(CultureInfo.InvariantCulture);
        if (Bool(settings, "swimsuits", true)) SwimsuitEnv(env, settings);
        else { env.Remove("RECOMP_DOAXBV_DISC"); env.Remove("RECOMP_COSTUME_PACK"); env.Remove("RECOMP_COSTUME_START_FAVORITE"); }
        // Test-harness settings would speed the game up or stop it after a timeout.
        env.Remove("RECOMP_UNPACED");
        env.Remove("RECOMP_WATCHDOG_MS");
        string logs = Private("play-logs");
        Directory.CreateDirectory(logs);
        string log = Path.Combine(logs, DateTime.Now.ToString("yyyyMMdd-HHmmss", CultureInfo.InvariantCulture) + ".log");
        start.Arguments = string.Format("/c \"\"{0}\" --xbe \"{1}\" 2>\"{2}\" >\"{2}.out\"\"", Runner, Path.Combine(disc, Str(record, "xbe", "default.xbe")), log);
        using (var game = Process.Start(start))
        {
            // A runner gone within seconds failed to start.
            if (!game.WaitForExit(5000)) return null;
        }
        string tail = "";
        try { tail = string.Join("\n", File.ReadAllLines(log).Reverse().Take(20).Reverse()); } catch (IOException) { }
        return "The game exited during startup:\n\n" + tail + "\n\nFull log: " + log;
    }
}

// Xbox controllers through XInput.
static class Pad
{
    [StructLayout(LayoutKind.Sequential)]
    struct State { public uint Packet; public ushort Buttons; public byte LT, RT; public short LX, LY, RX, RY; }
    [DllImport("xinput1_4.dll")] static extern int XInputGetState(int user, out State state);
    // Buttons of every connected controller, with the left stick folded into the D-pad bits.
    public static int Buttons()
    {
        int all = 0;
        for (int i = 0; i < 4; i++)
        {
            State s;
            if (XInputGetState(i, out s) != 0) continue;
            int b = s.Buttons;
            if (s.LY > 16000) b |= 1;
            if (s.LY < -16000) b |= 2;
            if (s.LX < -16000) b |= 4;
            if (s.LX > 16000) b |= 8;
            all |= b;
        }
        return all;
    }
}

class Row
{
    public string Label, Info, Action;
    public string[] Names;
    public int Index, Default;
}

class Page
{
    public string Name, Info, Action;
    public Row[] Rows;
}

class LauncherForm : Form
{
    static readonly int[] Heights = { 480, 720, 1080, 1440, 2160 };
    static readonly int[] Samples = { 1, 2, 4, 8 };
    static readonly int[] Volumes = { 0, 25, 50, 75, 100 };
    const int BuildPage = 4, PlayPage = 5;
    // Horizon Bar layout on a 960x540 window: letterbox bars, the settings and page tabs in the bottom one.
    const int W = 960, H = 540, TopH = 44, Side = 30, FadeY = 334;
    const int InfoY = 380, LabelY = 414, ValueY = 434, ValueH = 30, TabY = 494, TabH = 26;

    readonly Row resolution, msaa, smaa, screen, volume, freeCamera, swimsuits, startFavorite, chooseDisc, chooseGame, movies;
    readonly Page[] pages;
    // The menu cursor picks a page; inside a page the row cursor picks a setting.
    int menu, row;
    bool inPage;
    // A notice (build progress, a refused action) replaces the information text until the next input.
    string notice;
    string gameSource;
    string installed = Game.InstalledVersion();
    Process build;
    readonly string buildLog = Game.Private("build.log");
    readonly string doaxbvFile = Game.Private("doaxbv-path.txt");
    readonly Timer timer = new Timer { Interval = 500 };
    readonly Timer padTimer = new Timer { Interval = 50 };
    int padHeld;
    DateTime padRepeat = DateTime.MaxValue;
    Point? drag;
    RectangleF[] tabs;

    readonly Bitmap background = new Bitmap(W, H);
    readonly Font itemFont = new Font("Arial", 15, FontStyle.Bold | FontStyle.Italic, GraphicsUnit.Pixel);
    readonly Font menuFont = new Font("Arial", 19, FontStyle.Bold | FontStyle.Italic, GraphicsUnit.Pixel);
    readonly Font tabFont = new Font("Arial", 14, FontStyle.Bold | FontStyle.Italic, GraphicsUnit.Pixel);
    readonly Font infoFont = new Font("Arial", 14, FontStyle.Italic, GraphicsUnit.Pixel);
    readonly Font smallFont = new Font("Arial", 12, FontStyle.Bold, GraphicsUnit.Pixel);
    readonly Font titleFont = new Font("Arial", 25, FontStyle.Bold | FontStyle.Italic, GraphicsUnit.Pixel);
    readonly Font logoFont = new Font("Arial Black", 38, FontStyle.Italic, GraphicsUnit.Pixel);
    readonly Font bigFont = new Font("Arial Black", 66, FontStyle.Italic, GraphicsUnit.Pixel);
    readonly StringFormat far = new StringFormat { Alignment = StringAlignment.Far, LineAlignment = StringAlignment.Center };
    readonly StringFormat near = new StringFormat { Alignment = StringAlignment.Near, LineAlignment = StringAlignment.Center };
    readonly StringFormat center = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center };

    static int Nearest(int h) { return Math.Max(0, Heights.Count(x => x <= h) - 1); }
    static Row Toggle(string label, string info, bool on, int fallback)
    {
        return new Row { Label = label, Info = info, Names = new[] { "Off", "On" }, Index = on ? 1 : 0, Default = fallback };
    }

    public LauncherForm(string disc)
    {
        var settings = Game.LoadSettings();
        int screenHeight = Screen.PrimaryScreen.Bounds.Height;
        resolution = new Row { Label = "Resolution", Info = "Sets the render resolution. 480p opens in a window",
            Names = Heights.Select(h => h + "p").ToArray(), Index = Nearest(Game.Int(settings, "height", screenHeight)), Default = Nearest(screenHeight) };
        msaa = new Row { Label = "MSAA", Info = "Smooths polygon edges with multisampling", Names = new[] { "Off", "2x", "4x", "8x" },
            Index = Math.Max(0, Array.IndexOf(Samples, Game.Int(settings, "msaa", 1))) };
        smaa = Toggle("SMAA", "Smooths edges with a post-process filter", Game.Bool(settings, "smaa", false), 0);
        screen = new Row { Label = "Screen", Info = "Wider 3D view. The HUD and menus stay 4:3", Names = new[] { "4:3", "16:9" },
            Index = Game.Bool(settings, "widescreen", false) ? 1 : 0 };
        int savedVolume = Array.IndexOf(Volumes, Game.Int(settings, "volume", 100));
        volume = new Row { Label = "Volume", Info = "Sets the master volume", Names = Volumes.Select(v => v.ToString()).ToArray(),
            Index = savedVolume < 0 ? Volumes.Length - 1 : savedVolume, Default = Volumes.Length - 1 };
        // In a fight: click L3, then LB / LT (Player 1) or RB / RT (Player 2); X or B marks a favorite.
        freeCamera = Toggle("Free Camera", "In game, F1 or a right-stick click flies the camera", Game.Bool(settings, "free_camera", false), 0);
        swimsuits = Toggle("Swimsuits", "In a fight, click L3: LB / LT P1, RB / RT P2, X / B favorite, Y favorites only", Game.Bool(settings, "swimsuits", true), 1);
        startFavorite = Toggle("Start In Favorite", "Each fighter starts in her first favorite (L3 + X / B)", Game.Bool(settings, "swimsuit_start_favorite", false), 0);
        chooseDisc = new Row { Label = "DOAXBV Disc", Info = DiscName(), Action = "disc" };
        object source;
        gameSource = settings.TryGetValue("game_source", out source) ? source as string : null;
        chooseGame = new Row { Label = "DOA3 Disc", Info = SourceName(), Action = "game" };
        movies = Toggle("Movies", "Off saves 3.4 GB; the game then skips every movie", Game.Bool(settings, "movies", true), 1);
        var buildRow = new Row { Label = "Build", Info = "Lifts and compiles the game; takes 10-20 minutes", Action = "build" };
        pages = new[] {
            new Page { Name = "VIDEO SETTINGS", Info = "Sets resolution, anti-aliasing and screen shape", Rows = PageRows(resolution, msaa, smaa, screen) },
            new Page { Name = "AUDIO SETTINGS", Info = "Sets the master volume", Rows = PageRows(volume) },
            new Page { Name = "FREE CAMERA", Info = "Fly the camera through fights and cutscenes, freeze them and save screenshots", Rows = PageRows(freeCamera) },
            new Page { Name = "SWIMSUITS", Info = "DOAXBV swimsuits; needs your own DOAXBV disc (.iso or folder) to build them from", Rows = PageRows(swimsuits, startFavorite, chooseDisc) },
            new Page { Name = "BUILD GAME", Info = BuildStatus(), Rows = PageRows(chooseGame, movies, buildRow) },
            new Page { Name = "START GAME", Info = PlayInfo(), Rows = new Row[0], Action = "play" },
            new Page { Name = "EXIT", Info = "Closes the launcher", Rows = new Row[0], Action = "exit" },
        };
        menu = File.Exists(Game.Runner) ? PlayPage : BuildPage;

        Text = "Dead or Alive 3";
        FormBorderStyle = FormBorderStyle.None;
        StartPosition = FormStartPosition.CenterScreen;
        KeyPreview = true;
        BackColor = Color.Black;
        ClientSize = new Size(W, H);
        DoubleBuffered = true;
        AllowDrop = true;
        DrawBackground();
        timer.Tick += delegate { BuildTick(); };
        padTimer.Tick += delegate { PadTick(); };
        // Buttons already down at start (the press that opened the launcher) wait for a release.
        try { padHeld = Pad.Buttons(); padTimer.Start(); } catch (DllNotFoundException) { }
        if (disc != null && DiscPath(disc) != null) UseDisc(disc);
    }

    static Row[] PageRows(params Row[] rows)
    {
        return rows.Concat(new[] {
            new Row { Label = "Default Setting", Info = "Restores the defaults on this page", Action = "default" },
            new Row { Label = "Exit", Info = "Returns to the menu", Action = "back" } }).ToArray();
    }

    string DiscName()
    {
        string path = Game.FirstLine(doaxbvFile);
        return path != null ? "Disc: " + Path.GetFileName(path.TrimEnd('\\')) : @"Found at start in private\ or doaxbv-re; or choose or drop it here";
    }
    // Whether a disc was already extracted (private\imported-disc*\receipt.json).
    static bool Extracted()
    {
        string priv = Path.Combine(Game.Root, "private");
        return Directory.Exists(priv) && Directory.GetDirectories(priv, "imported-disc*").Any(d => File.Exists(Path.Combine(d, "receipt.json")));
    }
    string SourceName()
    {
        if (!string.IsNullOrEmpty(gameSource)) return "Disc: " + Path.GetFileName(gameSource.TrimEnd('\\'));
        return Extracted() ? "Disc: the one already extracted" : "No DOA3 disc chosen";
    }
    string BuildStatus()
    {
        return installed != null ? "Installed: " + installed + ". Build again after an update or for another disc" : "Not built yet: choose your DOA3 disc (Japan 3.1 or USA), then Build";
    }
    string PlayInfo()
    {
        return installed != null ? "Starts Dead or Alive 3 " + installed + " with these settings" : "Saves these settings and starts the game";
    }

    public Dictionary<string, object> Settings()
    {
        return new Dictionary<string, object> {
            { "height", Heights[resolution.Index] }, { "msaa", Samples[msaa.Index] }, { "smaa", smaa.Index == 1 },
            { "widescreen", screen.Index == 1 }, { "volume", Volumes[volume.Index] },
            { "free_camera", freeCamera.Index == 1 },
            { "swimsuits", swimsuits.Index == 1 }, { "swimsuit_start_favorite", startFavorite.Index == 1 },
            { "game_source", gameSource }, { "movies", movies.Index == 1 } };
    }

    static Color Rgb(int r, int g, int b, int a = 255) { return Color.FromArgb(a, r, g, b); }
    static Brush Gradient(float x, float y, float w, float h, Color c1, Color c2) { return new LinearGradientBrush(new RectangleF(x, y, w, h), c1, c2, 90f); }
    int ColW { get { return (W - 2 * Side) / Math.Max(Rows.Length, 1); } }
    static string Verb(string action) { return action == "build" ? "BUILD" : action == "default" ? "RESET" : action == "back" ? "BACK" : "CHOOSE"; }

    // The generated beach (tools/launcher-background.jpg, embedded by package_test.py) between letterbox bars.
    void DrawBackground()
    {
        using (var bg = Graphics.FromImage(background))
        using (var stream = typeof(LauncherForm).Assembly.GetManifestResourceStream("launcher-background.jpg"))
        {
            bg.InterpolationMode = InterpolationMode.HighQualityBicubic;
            if (stream != null) using (var image = Image.FromStream(stream)) bg.DrawImage(image, 0, 0, W, H);
            else bg.Clear(Rgb(74, 60, 51));
            bg.FillRectangle(new SolidBrush(Rgb(13, 10, 9)), 0, 0, W, TopH);
            var fade = new LinearGradientBrush(new Rectangle(0, FadeY - 1, W, H - FadeY + 2), Color.Black, Color.Black, 90f);
            fade.InterpolationColors = new ColorBlend { Colors = new[] { Rgb(13, 10, 9, 0), Rgb(13, 10, 9, 230), Rgb(13, 10, 9) }, Positions = new[] { 0f, .26f, 1f } };
            bg.FillRectangle(fade, 0, FadeY, W, H - FadeY);
        }
    }

    // Brushed silver, as on the title logo.
    static Brush Chrome(float y, float h)
    {
        var brush = new LinearGradientBrush(new RectangleF(0, y, 10, h), Color.White, Color.Gray, 90f);
        brush.InterpolationColors = new ColorBlend {
            Colors = new[] { Rgb(251, 251, 249), Rgb(212, 215, 216), Rgb(138, 143, 146), Rgb(201, 204, 204), Rgb(240, 240, 238) },
            Positions = new[] { 0f, .38f, .52f, .7f, 1f } };
        return brush;
    }

    static void Shadowed(Graphics g, string text, Font font, Brush brush, RectangleF rect, StringFormat format, int d)
    {
        g.DrawString(text, font, new SolidBrush(Rgb(30, 22, 18, 220)), new RectangleF(rect.X + d, rect.Y + d, rect.Width, rect.Height), format);
        g.DrawString(text, font, brush, rect, format);
    }

    // A triangle pointing along dx, dy from its base centred on x, y.
    static void Arrow(Graphics g, float x, float y, int dx, int dy, Color color)
    {
        g.FillPolygon(new SolidBrush(color), new[] { new PointF(x + 7 * dx, y + 7 * dy), new PointF(x - 5 * dy, y - 5 * dx), new PointF(x + 5 * dy, y + 5 * dx) });
    }

    static GraphicsPath Pill(float x, float y, float w, float h)
    {
        var path = new GraphicsPath();
        path.AddArc(x, y, h, h, 90, 180);
        path.AddArc(x + w - h, y, h, h, 270, 180);
        path.CloseFigure();
        return path;
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        Graphics g = e.Graphics;
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.TextRenderingHint = TextRenderingHint.AntiAlias;
        g.DrawImage(background, 0, 0);
        Color sand = Rgb(241, 197, 143), dark = Rgb(42, 29, 21), muted = Rgb(191, 178, 165);

        // Top letterbox bar: title and install state on the left, button prompts on the right.
        g.DrawString("DEAD OR ALIVE 3  \u00B7  " + (installed != null ? "INSTALLED: " + installed.ToUpperInvariant() : "NOT BUILT YET"),
            smallFont, new SolidBrush(muted), new RectangleF(Side, 0, 500, TopH), near);
        float x = W - Side;
        foreach (var hint in new[] { new[] { "", "CHANGE" }, new[] { "B", "BACK" }, new[] { "A", "SELECT" } })
        {
            float textW = g.MeasureString(hint[1], smallFont).Width;
            x -= textW;
            g.DrawString(hint[1], smallFont, new SolidBrush(muted), new RectangleF(x, 0, textW + 4, TopH), near);
            float w = hint[0] == "" ? 26 : 20;
            x -= w + 6;
            g.FillPath(new SolidBrush(Rgb(242, 227, 207)), Pill(x, TopH / 2 - 10, w, 20));
            if (hint[0] == "") { Arrow(g, x + 9, TopH / 2 + 2, 0, -1, dark); Arrow(g, x + 17, TopH / 2 - 2, 0, 1, dark); }
            else g.DrawString(hint[0], smallFont, new SolidBrush(dark), new RectangleF(x, TopH / 2 - 10, w, 20), center);
            x -= 22;
        }

        // Logo over the cliff.
        Shadowed(g, "3", bigFont, Chrome(58, 84), new RectangleF(0, 58, W - 44, 90), far, 3);
        Shadowed(g, "DEAD OR ALIVE", logoFont, Chrome(82, 50), new RectangleF(0, 82, W - 118, 56), far, 3);

        // The chosen page in the bottom bar: its information line, then its settings side by side.
        Page page = pages[menu];
        Row[] rows = page.Rows;
        string info = notice ?? (inPage ? rows[row].Info : page.Info);
        g.DrawString(info, infoFont, new SolidBrush(Rgb(216, 202, 188)), new RectangleF(Side, InfoY, W - 2 * Side, 22), near);
        if (page.Action == "play") Shadowed(g, "PRESS START BUTTON", titleFont, Chrome(ValueY - 8, 40), new RectangleF(Side, ValueY - 8, W - 2 * Side, 40), near, 2);
        var divider = new Pen(Rgb(255, 255, 255, 36));
        int colW = ColW;
        for (int i = 0; i < rows.Length; i++)
        {
            Row r = rows[i];
            int left = Side + i * colW;
            bool selected = inPage && i == row;
            if (selected) g.FillRectangle(new SolidBrush(Rgb(232, 191, 138, 45)), left, LabelY - 6, colW, ValueY + ValueH + 6 - LabelY);
            if (i > 0) g.DrawLine(divider, left, LabelY - 2, left, ValueY + ValueH);
            g.DrawString(r.Label.ToUpperInvariant(), smallFont, new SolidBrush(selected ? Color.White : Rgb(198, 182, 166)), new RectangleF(left + 12, LabelY, colW - 16, 18), near);
            var valueRect = new RectangleF(left + 26, ValueY, colW - 30, ValueH);
            if (r.Names == null) { g.DrawString(Verb(r.Action), itemFont, new SolidBrush(sand), new RectangleF(left + 12, ValueY, colW - 16, ValueH), near); continue; }
            string value = r.Names[r.Index].ToUpperInvariant();
            float cy = ValueY + ValueH / 2f;
            Color arrow = selected ? sand : Rgb(241, 197, 143, 130);
            Arrow(g, left + 19, cy, -1, 0, arrow);
            Arrow(g, left + 30 + g.MeasureString(value, menuFont).Width, cy, 1, 0, arrow);
            g.DrawString(value, menuFont, new SolidBrush(selected ? Color.White : Rgb(232, 222, 212)), valueRect, near);
        }

        // Page tabs along the bottom, the current one underlined in sunset gold.
        g.DrawLine(divider, Side, TabY - 8, W - Side, TabY - 8);
        tabs = new RectangleF[pages.Length];
        float tx = Side;
        for (int m = 0; m < pages.Length; m++)
        {
            float w = g.MeasureString(pages[m].Name, tabFont).Width;
            tabs[m] = new RectangleF(tx, TabY, w, TabH);
            g.DrawString(pages[m].Name, tabFont, new SolidBrush(m == menu ? Rgb(255, 243, 228) : Rgb(169, 156, 144)), tabs[m], near);
            if (m == menu) g.FillRectangle(new LinearGradientBrush(new RectangleF(tx - 1, 0, w + 2, 1), Rgb(247, 220, 181), Rgb(201, 138, 85), 0f), tx, TabY + TabH, w, 3);
            tx += w + 18;
        }
    }

    // Python 3.12 or newer, through the py launcher or PATH.
    static string[] FindPython()
    {
        foreach (var python in new[] { new[] { "py", "-3" }, new[] { "python" } })
        {
            string args = string.Join(" ", python.Skip(1)) + " -c \"import sys; sys.exit(sys.version_info < (3, 12))\"";
            try
            {
                using (var check = Process.Start(new ProcessStartInfo(python[0], args) { UseShellExecute = false, CreateNoWindow = true }))
                    if (check.WaitForExit(30000) && check.ExitCode == 0) return python;
            }
            catch (Win32Exception) { }
        }
        return null;
    }

    // A trailing backslash would escape the closing quote.
    static string Quote(string path) { return "\"" + (path.EndsWith("\\") ? path + "." : path) + "\""; }

    void StartBuild()
    {
        if (build != null) return;
        if (string.IsNullOrEmpty(gameSource) && !Extracted()) { notice = "Choose your DOA3 ISO or disc folder first"; return; }
        string[] python = FindPython();
        if (python == null)
        {
            MessageBox.Show("Install Python 3.12 or newer from https://www.python.org/downloads/windows/ and add it to PATH, then build again.", "DOA3 launcher");
            return;
        }
        Game.SaveSettings(Settings());
        var command = new List<string> { python[0] };
        command.AddRange(python.Skip(1));
        command.Add("-u");
        command.Add(Quote(Path.Combine(Game.Root, @"tools\build_game.py")));
        if (!string.IsNullOrEmpty(gameSource)) command.Add(Quote(gameSource));
        command.Add(movies.Index == 1 ? "--movies" : "--no-movies");
        command.Add(">" + Quote(buildLog));
        command.Add("2>" + Quote(buildLog + ".err"));
        var start = new ProcessStartInfo("cmd.exe", "/c \"" + string.Join(" ", command) + "\"")
            { UseShellExecute = false, CreateNoWindow = true, WorkingDirectory = Game.Root };
        // Build tools print any character; the log is read back as UTF-8.
        start.EnvironmentVariables["PYTHONIOENCODING"] = "utf-8";
        build = Process.Start(start);
        notice = "Starting the build";
        timer.Start();
    }

    static string Tail(string path, int count)
    {
        try
        {
            using (var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
            {
                stream.Seek(Math.Max(0, stream.Length - 8192), SeekOrigin.Begin);
                var lines = new StreamReader(stream, Encoding.UTF8).ReadToEnd().Split('\n').Select(l => l.Trim()).Where(l => l.Length > 0).ToArray();
                return string.Join("\n", lines.Skip(Math.Max(0, lines.Length - count)));
            }
        }
        catch (IOException) { return ""; }
    }

    void BuildTick()
    {
        string line = Tail(buildLog, 1);
        if (line.Length > 0) notice = line;
        if (build.HasExited) { timer.Stop(); FinishBuild(); }
        Invalidate();
    }

    void FinishBuild()
    {
        int code = build.ExitCode;
        build.Dispose();
        build = null;
        installed = Game.InstalledVersion();
        pages[BuildPage].Info = BuildStatus();
        pages[PlayPage].Info = PlayInfo();
        chooseGame.Info = SourceName();
        if (code == 0) { notice = "Build complete. Choose START GAME to play"; menu = PlayPage; inPage = false; return; }
        notice = @"Build failed; see private\build.log";
        MessageBox.Show("The build failed:\n\n" + Tail(buildLog + ".err", 8) + "\n\nFull output: " + buildLog, "DOA3 launcher");
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        base.OnFormClosing(e);
        if (build == null || build.HasExited) return;
        if (MessageBox.Show("Stop the build?", "DOA3 launcher", MessageBoxButtons.YesNo) != DialogResult.Yes) { e.Cancel = true; return; }
        timer.Stop();
        using (var kill = Process.Start(new ProcessStartInfo("taskkill", "/T /F /PID " + build.Id) { UseShellExecute = false, CreateNoWindow = true }))
            kill.WaitForExit();
    }

    static int Clamp(int value, int max) { return Math.Min(Math.Max(value, 0), max); }
    Row[] Rows { get { return pages[menu].Rows; } }

    void MoveCursor(int delta)
    {
        if (inPage) row = Clamp(row + delta, Rows.Length - 1);
        else menu = Clamp(menu + delta, pages.Length - 1);
    }

    void ChangeValue(int delta)
    {
        if (!inPage) return;
        Row r = Rows[row];
        if (r.Names != null) r.Index = Clamp(r.Index + delta, r.Names.Length - 1);
    }

    static string Choose(string title, string filter)
    {
        using (var dialog = new OpenFileDialog { Title = title, Filter = filter })
            return dialog.ShowDialog() == DialogResult.OK ? dialog.FileName : null;
    }

    void Accept()
    {
        Page page = pages[menu];
        if (!inPage)
        {
            if (page.Action == "play" && build != null) notice = "Wait for the build to finish";
            else if (page.Action == "play" && !File.Exists(Game.Runner)) { menu = BuildPage; notice = "Build the game first"; }
            else if (page.Action != null) { Tag = page.Action; Close(); return; }
            else { inPage = true; row = 0; }
        }
        else
        {
            Row r = page.Rows[row];
            if (r.Action == "default") { foreach (Row each in page.Rows) if (each.Names != null) each.Index = each.Default; }
            else if (r.Action == "back") inPage = false;
            else if (r.Action == "disc")
            {
                string file = Choose("DOAXBV disc image (.iso), or datahdd.afs in its extracted folder", "DOAXBV disc|*.iso;datahdd.afs|All files|*.*");
                if (file != null)
                {
                    Directory.CreateDirectory(Path.GetDirectoryName(doaxbvFile));
                    File.WriteAllText(doaxbvFile, file.EndsWith(".afs", StringComparison.OrdinalIgnoreCase) ? Path.GetDirectoryName(file) : file);
                    r.Info = DiscName();
                }
            }
            else if (r.Action == "game")
            {
                string file = Choose("Dead or Alive 3 disc image (.iso), or default.xbe in its extracted folder", "DOA3 disc|*.iso;default.xbe|All files|*.*");
                if (file != null)
                {
                    gameSource = file.EndsWith(".xbe", StringComparison.OrdinalIgnoreCase) ? Path.GetDirectoryName(file) : file;
                    r.Info = SourceName();
                }
            }
            else if (r.Action == "build") StartBuild();
        }
        Invalidate();
    }

    void Back()
    {
        if (inPage) inPage = false;
        else Close();
    }

    void Press(Keys key)
    {
        if (build == null) notice = null;
        switch (key)
        {
            case Keys.Left: MoveCursor(-1); break;
            case Keys.Right: MoveCursor(1); break;
            case Keys.Up: if (inPage) ChangeValue(1); break;
            case Keys.Down: if (inPage) ChangeValue(-1); else if (Rows.Length > 0) Accept(); break;
            case Keys.Return: Accept(); break;
            case Keys.Escape: case Keys.Back: Back(); break;
        }
        Invalidate();
    }

    // A form without controls would otherwise spend the arrow keys on focus moves.
    protected override bool ProcessDialogKey(Keys keyData) { return false; }
    protected override void OnKeyDown(KeyEventArgs e) { base.OnKeyDown(e); Press(e.KeyCode); }

    // A dropped DOAXBV disc (a folder with datahdd.afs, that file, or an ISO named for the game) becomes the
    // swimsuit disc; any other ISO, disc folder or default.xbe becomes the DOA3 disc and selects Build.
    static string DiscPath(string path)
    {
        return new[] { ".iso", ".xbe", ".afs" }.Any(e => path.EndsWith(e, StringComparison.OrdinalIgnoreCase)) || Directory.Exists(path) ? path : null;
    }
    static string DoaxbvPath(string path)
    {
        if (path.EndsWith(".afs", StringComparison.OrdinalIgnoreCase)) return Path.GetDirectoryName(path);
        if (File.Exists(Path.Combine(path, "datahdd.afs")) || Game.IsDoaxbvImage(path)) return path;
        return null;
    }

    void UseDisc(string path)
    {
        string doaxbv = DoaxbvPath(path);
        if (doaxbv != null)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(doaxbvFile));
            File.WriteAllText(doaxbvFile, doaxbv);
            chooseDisc.Info = DiscName();
            notice = "Swimsuits: " + DiscName();
            return;
        }
        if (build != null) { notice = "Wait for the build to finish"; return; }
        gameSource = path.EndsWith(".xbe", StringComparison.OrdinalIgnoreCase) ? Path.GetDirectoryName(path) : path;
        chooseGame.Info = SourceName();
        menu = BuildPage; inPage = true; row = 2;
        notice = SourceName() + ". Press Build";
    }

    static string Dropped(DragEventArgs e)
    {
        var files = e.Data.GetData(DataFormats.FileDrop) as string[];
        return files != null && files.Length > 0 ? DiscPath(files[0]) : null;
    }
    protected override void OnDragEnter(DragEventArgs e) { if (Dropped(e) != null) e.Effect = DragDropEffects.Copy; }
    protected override void OnDragDrop(DragEventArgs e)
    {
        string path = Dropped(e);
        if (path != null) { UseDisc(path); Activate(); Invalidate(); }
    }

    // D-pad or left stick, A or START, B or BACK. Directions repeat while held.
    static readonly int[] PadBits = { 0x1, 0x2, 0x4, 0x8, 0x1000, 0x10, 0x2000, 0x20 };
    static readonly Keys[] PadKeys = { Keys.Up, Keys.Down, Keys.Left, Keys.Right, Keys.Return, Keys.Return, Keys.Escape, Keys.Escape };
    void PadTick()
    {
        // Only while the launcher has focus, so a controller used elsewhere during a build is ignored.
        if (ActiveForm != this) return;
        int buttons = Pad.Buttons();
        DateTime now = DateTime.Now;
        int held = padHeld;
        padHeld = buttons;
        for (int i = 0; i < PadBits.Length; i++)
        {
            int bit = PadBits[i];
            if ((buttons & bit) == 0) continue;
            bool pressed = (held & bit) == 0;
            if (!pressed && (bit > 0x8 || now < padRepeat)) continue;
            padRepeat = now.AddMilliseconds(pressed ? 400 : 120);
            Press(PadKeys[i]);
            return;
        }
    }

    int HitTab(int x, int y)
    {
        if (tabs == null) return -1;
        for (int m = 0; m < tabs.Length; m++) if (tabs[m].Contains(x, y)) return m;
        return -1;
    }
    int HitColumn(int x, int y)
    {
        if (y < LabelY - 6 || y > ValueY + ValueH || x < Side) return -1;
        int i = (x - Side) / ColW;
        return i < Rows.Length ? i : -1;
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        // Borderless, so a drag on the top bar moves the window.
        if (drag.HasValue) { Location = new Point(Left + e.X - drag.Value.X, Top + e.Y - drag.Value.Y); return; }
        int m = HitTab(e.X, e.Y), i = HitColumn(e.X, e.Y);
        if (!inPage && m >= 0 && m != menu) { menu = m; Invalidate(); }
        if (inPage && i >= 0 && i != row) { row = i; Invalidate(); }
    }
    protected override void OnMouseDown(MouseEventArgs e) { if (e.Y < TopH) drag = e.Location; }
    protected override void OnMouseUp(MouseEventArgs e) { drag = null; }

    protected override void OnMouseClick(MouseEventArgs e)
    {
        if (build == null) notice = null;
        int x = e.X, m = HitTab(x, e.Y), i = HitColumn(x, e.Y);
        if (e.Button == MouseButtons.Right) { Back(); Invalidate(); return; }
        if (m >= 0) { menu = m; inPage = false; Accept(); return; }
        if (i < 0) return;
        // From the tabs, the first click only enters the page and selects the setting.
        bool entering = !inPage;
        inPage = true; row = i;
        if (entering) { Invalidate(); return; }
        if (Rows[i].Action != null) Accept();
        else ChangeValue(x < Side + i * ColW + ColW / 2 ? -1 : 1);
        Invalidate();
    }
}

static class Program
{
    [STAThread]
    static void Main(string[] args)
    {
        Application.EnableVisualStyles();
        try
        {
#if PLAY_ONLY
            if (!File.Exists(Game.Runner)) { Process.Start(Path.Combine(Game.Root, "Launcher.exe")); return; }
            Dictionary<string, object> settings = Game.LoadSettings();
#else
            var form = new LauncherForm(args.Length > 0 ? args[0] : null);
            Application.Run(form);
            if (form.Tag as string != "play") return;
            Dictionary<string, object> settings = form.Settings();
            Game.SaveSettings(settings);
#endif
            string error = Game.Play(settings);
            if (error != null) MessageBox.Show(error, "Dead or Alive 3");
        }
        catch (Exception e) { MessageBox.Show(e.Message, "Dead or Alive 3"); }
    }
}
