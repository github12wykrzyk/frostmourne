using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Windows.Forms;

namespace FrostmourneGui {
    internal sealed class Module {
        internal string Path = "";
        internal bool Enabled = true;
        internal string Status = "NIEPRZETESTOWANE";
        internal string Hash = "";
        internal string Version = "?";
        internal string Arch = "?";
        internal string Id = "";
        internal ModuleManifest Manifest;
        internal readonly Dictionary<string,string> Options = new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);
    }

    internal static class Verify {
        internal const string ReferenceSha = "edba72ae4188bda717eec73b733aab9cb2f4ab7d4a1e22b44e60d81743648ebd";
        internal const long ReferenceSize = 7704216;
        internal static string Hash(string path) {
            using (FileStream f = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read))
            using (SHA256 sha = SHA256.Create())
                return BitConverter.ToString(sha.ComputeHash(f)).Replace("-", "").ToLowerInvariant();
        }
        internal static void Pe32(string path, bool dll) {
            byte[] b = File.ReadAllBytes(path);
            if (b.Length < 256 || b[0] != 'M' || b[1] != 'Z') throw new InvalidDataException("Brak naglowka MZ");
            int p = BitConverter.ToInt32(b, 0x3c);
            if (p < 0 || p > b.Length - 26 || b[p] != 'P' || b[p+1] != 'E' || b[p+2] != 0 || b[p+3] != 0)
                throw new InvalidDataException("Nieprawidlowy naglowek PE");
            if (BitConverter.ToUInt16(b, p+4) != 0x14c || BitConverter.ToUInt16(b, p+24) != 0x10b)
                throw new InvalidDataException("Wymagany format PE32 / I386 (x86)");
            bool actualDll = (BitConverter.ToUInt16(b, p+22) & 0x2000) != 0;
            if (actualDll != dll) throw new InvalidDataException("Niezgodny typ pliku EXE/DLL");
        }
        internal static string Enc(string value) { return Convert.ToBase64String(Encoding.UTF8.GetBytes(value)); }
        internal static string Dec(string value) { return Encoding.UTF8.GetString(Convert.FromBase64String(value)); }
    }

    internal sealed class MainWindow : Form {
        readonly string home = System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Frostmourne");
        readonly string folder;
        readonly string settings;
        readonly string log;
        readonly TextBox exe = new TextBox();
        readonly TextBox console = new TextBox();
        readonly ListView modules = new ListView();
        readonly List<Module> selected = new List<Module>();
        readonly Label clientInfo = new Label();
        readonly Label gameInfo = new Label();
        readonly Label dllInfo = new Label();
        readonly Label pidInfo = new Label();
        readonly Label resultInfo = new Label();
        readonly Timer poll = new Timer();
        readonly CheckBox kickTrial = new CheckBox();
        readonly NumericUpDown kickWindow = new NumericUpDown();
        readonly Label updateInfo = new Label();
        readonly CheckBox autoUpdates = new CheckBox();
        readonly ComboBox updateChannel = new ComboBox();
        readonly Button applyUpdate = new Button();
        bool updateBusy = false;
        Process game;
        string verifiedExe = "";
        string verifiedHash = "";
        string pinnedHash = "";
        long pinnedSize = 0;
        bool loading = false;
        bool checking = false;
        bool migratedLegacyDll = false;
        string dllState = "NIEPRZETESTOWANE";

        internal MainWindow() {
            folder = System.IO.Path.Combine(home, "logs");
            settings = System.IO.Path.Combine(home, "loader.cfg");
            log = System.IO.Path.Combine(folder, "gui-loader-" + DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fff") + ".log");
            Directory.CreateDirectory(folder);
            Text = "FROSTMOURNE  |  DLL Loader x86 (experimental)";
            Size = new Size(1060, 895);
            MinimumSize = new Size(1060, 895);
            StartPosition = FormStartPosition.CenterScreen;
            BackColor = Color.FromArgb(19, 23, 33);
            ForeColor = Color.FromArgb(226, 235, 246);
            Font = new Font("Segoe UI", 9F);
            BuildUi();
            ReadPin();
            Restore();
            kickTrial.CheckedChanged += (s,e) => Save();
            kickWindow.ValueChanged += (s,e) => Save();
            autoUpdates.CheckedChanged += (s,e) => Save();
            updateChannel.SelectedIndexChanged += (s,e) => Save();
            selected.RemoveAll(m => m.Manifest == null && !String.Equals(System.IO.Path.GetFullPath(m.Path), System.IO.Path.GetFullPath(System.IO.Path.Combine(Application.StartupPath, "FrostmourneBootstrap.dll")), StringComparison.OrdinalIgnoreCase));
            List<Module> discovered = ModuleCatalog.Discover(Write);
            ModuleCatalog.Restore(discovered, Write);
            if (discovered.Count == 0) AddDefaultDll();
            else { selected.Clear(); selected.AddRange(discovered); }
            if (migratedLegacyDll) Save(); // Persist migration so stale bootstrap paths cannot block subsequent launches.
            RefreshModules();
            poll.Interval = 1000;
            poll.Tick += (s, e) => PollGame();
            poll.Start();
            Write("GUI START: kontrolowane x86 LoadLibraryW -> FrostmourneBootstrap ABI; bez hookow i ukrywania.");
            Write("Zgodnosc klienta: 3.3.5a build 12340, PE32 x86, scisly SHA256.");
            FormClosing += (s, e) => { Save(); poll.Stop(); /* Nie zamykaj gry przy zamknieciu GUI. */ };
            Shown += (s,e) => { if (autoUpdates.Checked) UpdateAsync(true); };
        }
        Label Label(string text, int x, int y, int w, int h, int size = 9) {
            Label l = new Label { Text = text, Location = new Point(x,y), Size = new Size(w,h),
                Font = new Font("Segoe UI", size, size >= 13 ? FontStyle.Bold : FontStyle.Regular),
                ForeColor = Color.FromArgb(218,229,244) };
            Controls.Add(l); return l;
        }
        Button Button(string text, int x, int y, int w, int h, EventHandler action) {
            Button b = new Button { Text = text, Location = new Point(x,y), Size = new Size(w,h),
                BackColor = Color.FromArgb(48,68,94), ForeColor = Color.White, FlatStyle = FlatStyle.Flat,
                Cursor = Cursors.Hand, UseVisualStyleBackColor = false };
            b.FlatAppearance.BorderColor = Color.FromArgb(91,114,142);
            b.Click += action; Controls.Add(b); return b;
        }
        void BuildUi() {
            Label("FROSTMOURNE  /  CLIENT LAUNCH & DLL AUDIT", 22, 15, 990, 36, 16);
            Label("KLIENT GRY", 24, 64, 950, 24, 12);
            exe.Location = new Point(24, 94); exe.Size = new Size(782, 26); exe.ReadOnly = true;
            exe.BackColor = Color.FromArgb(34,42,56); exe.ForeColor = Color.White; Controls.Add(exe);
            Button("Wybierz plik EXE", 816, 92, 205, 30, (s,e) => PickExe());
            Place(clientInfo, "Fingerprint: NIEZWERYFIKOWANY", 24, 129, 995, 24);
            Place(gameInfo, "Gra: NIEURUCHOMIONA", 24, 157, 650, 24);
            Place(pidInfo, "PID: --", 700, 157, 315, 24);
            Label("BIBLIOTEKI DLL", 24, 199, 750, 26, 12);
            Button("Dodaj DLL", 744, 195, 132, 30, (s,e) => PickDll());
            Button("Ustawienia modulu", 884, 195, 137, 30, (s,e) => ConfigureSelected());
            modules.Location = new Point(24, 230); modules.Size = new Size(997, 151);
            modules.View = View.Details; modules.FullRowSelect = true; modules.CheckBoxes = true;
            modules.GridLines = true; modules.HideSelection = false;
            modules.BackColor = Color.FromArgb(28,35,48); modules.ForeColor = Color.White;
            modules.Columns.Add("Modul", 185); modules.Columns.Add("Wersja / ABI", 110);
            modules.Columns.Add("Arch", 58); modules.Columns.Add("SHA256", 255);
            modules.Columns.Add("Weryfikacja / inicjalizacja", 365);
            modules.ItemChecked += (s,e) => {
                if (!loading && e.Item.Tag is Module) {
                    Module candidate = (Module)e.Item.Tag;
                    if (candidate.Manifest == null || candidate.Status.StartsWith("ZWERYFIKOWANY") || candidate.Status.StartsWith("PASS")) candidate.Enabled = e.Item.Checked;
                    else { candidate.Enabled = false; Write("BLOKADA wlaczenia " + candidate.Path + " " + candidate.Status); RefreshModules(); }
                    Save();
                }
            };
            Controls.Add(modules);
            kickTrial.Text = "AUTO KICK - EKSPERYMENT (wymaga Rogue, tylko zaznaczony cel)";
            kickTrial.Location = new Point(24, 387); kickTrial.Size = new Size(550, 32);
            kickTrial.BackColor = Color.FromArgb(19,23,33); kickTrial.ForeColor = Color.FromArgb(245,221,154);
            kickTrial.Checked = false; Controls.Add(kickTrial);
            Label("Pozostaly czas (ms)", 594, 389, 226, 24);
            kickWindow.Location = new Point(830, 387); kickWindow.Size = new Size(172, 25);
            kickWindow.Minimum = 200; kickWindow.Maximum = 3000; kickWindow.Increment = 50;
            kickWindow.Value = 800; Controls.Add(kickWindow);
            Place(dllInfo, "DLL w procesie gry: NIEPRZETESTOWANE", 24, 425, 995, 26);
            Button("URUCHOM WOW", 24, 460, 997, 52, (s,e) => Launch());
            Place(resultInfo, "TEST DLL W WOW: NIEPRZETESTOWANE", 24, 521, 995, 28);
            resultInfo.Font = new Font("Segoe UI", 12, FontStyle.Bold);
            Label("DIAGNOSTYKA  /  logi i bledy procesu", 24, 557, 650, 25, 12);
            Button("Otworz katalog logow", 801, 554, 220, 30, (s,e) => {
                try { Process.Start("explorer.exe", folder); } catch(Exception ex) { Write("Otwarcie logow: " + ex.Message); }
            });
            console.Location = new Point(24, 591); console.Size = new Size(997, 159);
            console.Multiline = true; console.ScrollBars = ScrollBars.Vertical; console.ReadOnly = true;
            console.BackColor = Color.FromArgb(12,17,25); console.ForeColor = Color.FromArgb(177,221,189);
            console.Font = new Font("Consolas", 9F); Controls.Add(console);
            updateChannel.DropDownStyle = ComboBoxStyle.DropDownList;
            updateChannel.Items.AddRange(new object[] { "work", "stable" });
            updateChannel.SelectedIndex = 0;
            updateChannel.Location = new Point(24, 762); updateChannel.Size = new Size(110, 28);
            Controls.Add(updateChannel);
            autoUpdates.Text = "Aktualizuj automatycznie przy starcie";
            autoUpdates.Checked = true; autoUpdates.Location = new Point(145, 760);
            autoUpdates.Size = new Size(262, 30); Controls.Add(autoUpdates);
            Button("SPRAWDZ AKTUALIZACJE", 415, 760, 215, 32, (s,e) => UpdateAsync(false));
            applyUpdate.Text = "AKTUALIZUJ"; applyUpdate.Location = new Point(645, 760);
            applyUpdate.Size = new Size(175, 32); applyUpdate.BackColor = Color.FromArgb(48,68,94);
            applyUpdate.ForeColor = Color.White; applyUpdate.Click += (s,e) => UpdateAsync(true);
            Controls.Add(applyUpdate);
            Place(updateInfo, "GitHub: jeszcze nie sprawdzono", 24, 800, 995, 25);
        }
        Label Place(Label l, string value, int x, int y, int w, int h) {
            l.Text = value; l.Location = new Point(x,y); l.Size = new Size(w,h); Controls.Add(l); return l;
        }
        void Write(string message) {
            string line = DateTime.UtcNow.ToString("yyyy-MM-ddTHH:mm:ss.fffZ") + " " + message + Environment.NewLine;
            console.AppendText(line);
            try { File.AppendAllText(log, line, Encoding.UTF8); }
            catch (Exception ex) { console.AppendText("BLAD ZAPISU LOGU: " + ex.Message + Environment.NewLine); }
        }
        void ReadPin() {
            try {
                string[] parts = File.ReadAllText(System.IO.Path.Combine(Application.StartupPath, "bootstrap.sha256")).Trim().Split(' ');
                if (parts.Length != 2 || parts[0].Length != 64) throw new InvalidDataException("Bledny bootstrap.sha256");
                foreach(char c in parts[0]) if (!Uri.IsHexDigit(c)) throw new InvalidDataException("Bledny SHA256");
                pinnedHash = parts[0].ToLowerInvariant();
                if (!long.TryParse(parts[1], out pinnedSize) || pinnedSize < 256) throw new InvalidDataException("Bledny rozmiar DLL");
                Write("Manifest DLL: sha256=" + pinnedHash + " size=" + pinnedSize);
            } catch (Exception ex) { if (File.Exists(System.IO.Path.Combine(Application.StartupPath, "bootstrap.sha256"))) Write("FAIL manifest legacy DLL: " + ex.Message); pinnedHash = ""; }
        }
        void AddDefaultDll() {
            string p = System.IO.Path.Combine(Application.StartupPath, "FrostmourneBootstrap.dll");
            if (File.Exists(p) && !selected.Exists(m => String.Equals(m.Path, p, StringComparison.OrdinalIgnoreCase)))
                selected.Insert(0, new Module { Path = p });
        }
        void PickExe() {
            using (OpenFileDialog d = new OpenFileDialog()) {
                d.Filter = "Programy EXE|*.exe|Wszystkie pliki|*.*"; d.CheckFileExists = true;
                if (d.ShowDialog(this) == DialogResult.OK) {
                    exe.Text = d.FileName; verifiedExe = ""; verifiedHash = "";
                    clientInfo.Text = "Fingerprint: NIEZWERYFIKOWANY"; Save();
                    Write("Wybrano klienta: " + d.FileName);
                }
            }
        }
        void PickDll() {
            using (OpenFileDialog d = new OpenFileDialog()) {
                d.Filter = "Biblioteki DLL|*.dll"; d.Multiselect = true;
                if (d.ShowDialog(this) == DialogResult.OK) {
                    foreach(string path in d.FileNames) {
                        if (!selected.Exists(m => String.Equals(m.Path, path, StringComparison.OrdinalIgnoreCase)))
                            selected.Add(new Module { Path = path });
                    }
                    Save(); RefreshModules();
                }
            }
        }
        void ConfigureSelected() {
            if (modules.SelectedItems.Count != 1) { Write("Wybierz jeden modul, aby edytowac ustawienia."); return; }
            Module m = modules.SelectedItems[0].Tag as Module;
            if (m == null || m.Manifest == null) { Write("Brak manifestu opcji dla wybranego modulu."); return; }
            using (ModuleOptionsDialog dialog = new ModuleOptionsDialog(m)) {
                if (dialog.ShowDialog(this) == DialogResult.OK) { Save(); RefreshModules(); }
            }
        }
        void RemoveDll() {
            if (modules.SelectedItems.Count == 0) return;
            Module m = modules.SelectedItems[0].Tag as Module;
            if (m != null) { selected.Remove(m); Save(); RefreshModules(); }
        }
        bool VerifyExe() {
            try {
                string path = exe.Text;
                if (!File.Exists(path)) throw new FileNotFoundException("Wybierz istniejacy plik EXE");
                if (!String.Equals(System.IO.Path.GetExtension(path), ".exe", StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException("Wybrany plik musi miec rozszerzenie .exe");
                Verify.Pe32(path, false);
                if (new FileInfo(path).Length != Verify.ReferenceSize)
                    throw new InvalidDataException("Niezgodny rozmiar klienta (oczekiwane 7704216 B)");
                string h = Verify.Hash(path);
                if (h != Verify.ReferenceSha) throw new InvalidDataException("Niezgodny SHA256 klienta: " + h);
                string v = FileVersionInfo.GetVersionInfo(path).FileVersion ?? "?";
                if (v.Replace(", ", ".").Replace(",", ".").Replace(" ", "") != "3.3.5.12340")
                    throw new InvalidDataException("Niepoprawna wersja klienta: " + v);
                verifiedExe = path; verifiedHash = h;
                clientInfo.Text = "PASS  |  WoW 3.3.5a / 12340  |  PE32 x86  |  SHA256 " + h;
                Write("PASS klient: wersja=" + v + " PE32 x86 size=7704216 sha256=" + h);
                return true;
            } catch(Exception ex) {
                clientInfo.Text = "FAIL  |  " + ex.Message; verifiedExe = ""; verifiedHash = "";
                Write("FAIL klient: " + ex); return false;
            }
        }
        bool VerifyDlls() {
            bool ok = true;
            foreach(Module m in selected) {
                if (!m.Enabled) { m.Status = "WYLACZONY"; continue; }
                try {
                    Verify.Pe32(m.Path, true);
                    m.Arch = "x86";
                    m.Hash = Verify.Hash(m.Path);
                    m.Version = FileVersionInfo.GetVersionInfo(m.Path).FileVersion ?? "?";
                    if (m.Manifest == null) {
                        if (!String.Equals(System.IO.Path.GetFullPath(m.Path),
                              System.IO.Path.GetFullPath(System.IO.Path.Combine(Application.StartupPath, "FrostmourneBootstrap.dll")),
                              StringComparison.OrdinalIgnoreCase) || pinnedHash.Length != 64 ||
                            m.Hash != pinnedHash || new FileInfo(m.Path).Length != pinnedSize)
                            throw new InvalidDataException("Legacy DLL rozni sie od przypietej binarki");
                        m.Version = "0.1.0-legacy / ABI 1.0";
                    } else {
                        if (!String.Equals(m.Hash, m.Manifest.sha256, StringComparison.OrdinalIgnoreCase))
                            throw new InvalidDataException("SHA256 niezgodne z manifestem modulu " + m.Id);
                        if (m.Manifest.abi_major != 1 || m.Manifest.abi_minor != 0 ||
                            m.Manifest.client_sha256 != Verify.ReferenceSha || m.Manifest.architecture != "x86")
                            throw new InvalidDataException("Niezgodny manifest ABI lub klienta " + m.Id);
                        foreach (ModuleAsset asset in m.Manifest.assets ?? new ModuleAsset[0]) {
                            string assetFile = System.IO.Path.GetFullPath(System.IO.Path.Combine(System.IO.Path.GetDirectoryName(m.Path), asset.path));
                            if (!File.Exists(assetFile) || !String.Equals(Verify.Hash(assetFile), asset.sha256, StringComparison.OrdinalIgnoreCase))
                                throw new InvalidDataException("Brak lub niezgodny plik dodatkowy " + asset.path);
                        }
                    }
                    m.Status = "PASS integralnosc; LOAD: NIEPRZETESTOWANE";
                    Write("PASS DLL " + m.Path + " hash=" + m.Hash + " abi=1.0; load=not-tested");
                } catch(Exception ex) { m.Status = "FAIL: " + ex.Message; ok = false; Write("FAIL DLL " + m.Path + ": " + ex); }
            }
            if (ok && selected.Exists(m => m.Enabled && m.Manifest != null)) {
                try { ModuleCatalog.Order(selected); }
                catch (Exception e) { Write("FAIL zaleznosci: " + e.Message); ok = false; }
            }
            RefreshModules();
            dllInfo.Text = ok ? "DLL: zweryfikowane na dysku; wewnatrz Wow.exe: NIEPRZETESTOWANE"
                              : "DLL: FAIL weryfikacji; proces gry nie bedzie uruchomiony";
            return ok;
        }
        void RefreshModules() {
            loading = true; modules.BeginUpdate();
            try {
                modules.Items.Clear();
                foreach(Module m in selected) {
                    ListViewItem i = new ListViewItem(m.Manifest == null ? System.IO.Path.GetFileName(m.Path) : m.Id);
                    i.SubItems.Add(m.Version); i.SubItems.Add(m.Arch); i.SubItems.Add(m.Hash); i.SubItems.Add(m.Status);
                    i.Tag = m; i.Checked = m.Enabled; modules.Items.Add(i);
                }
            } finally { modules.EndUpdate(); loading = false; }
        }
        // Install only the bundled diagnostic addon beside the VERIFIED game executable.
        // DLL injection and Lua addons are separate subsystems. Loading a DLL never registers /fmcast.
        void InstallCastProbeAddon() {
            string src = System.IO.Path.Combine(Application.StartupPath, "FrostmourneCastProbe");
            string sourceToc = System.IO.Path.Combine(src, "FrostmourneCastProbe.toc");
            string sourceLua = System.IO.Path.Combine(src, "FrostmourneCastProbe.lua");
            if (!File.Exists(sourceToc) || !File.Exists(sourceLua))
                throw new FileNotFoundException("W paczce brakuje folderu FrostmourneCastProbe (TOC/LUA); wypakuj CALY ZIP.");
            string tocText = File.ReadAllText(sourceToc);
            string luaText = File.ReadAllText(sourceLua);
            if (!tocText.Contains("## Interface: 30300") ||
                !tocText.Contains("FrostmourneCastProbe.lua") ||
                !luaText.Contains("SLASH_FROSTMOURNECASTPROBE1") ||
                !luaText.Contains("SlashCmdList[\"FROSTMOURNECASTPROBE\"]"))
                throw new InvalidDataException("Niepoprawna zawartosc dolaczonego dodatku FrostmourneCastProbe");
            if (!luaText.Contains("local kickRequests = false"))
                throw new InvalidDataException("Brak bezpiecznej konfiguracji testu Kick w dolaczonym Lua");
            string target = System.IO.Path.Combine(System.IO.Path.GetDirectoryName(verifiedExe),
                "Interface", "AddOns", "FrostmourneCastProbe");
            Directory.CreateDirectory(target);
            foreach (string fileName in new[] { "FrostmourneCastProbe.toc", "FrostmourneCastProbe.lua" }) {
                string from = System.IO.Path.Combine(src, fileName);
                string to = System.IO.Path.Combine(target, fileName);
                string payload = File.ReadAllText(from);
                if (fileName.EndsWith(".lua", StringComparison.OrdinalIgnoreCase) && kickTrial.Checked)
                    payload = payload.Replace("local kickRequests = false", "local kickRequests = true");
                byte[] bytes = Encoding.UTF8.GetBytes(payload);
                string expected;
                using (SHA256 sha = SHA256.Create())
                    expected = BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-", "").ToLowerInvariant();
                if (File.Exists(to) && Verify.Hash(to) != expected) {
                    // Preserve previous diagnostic versions or locally modified addon files.
                    string prior = File.ReadAllText(to);
                    bool ours = fileName.EndsWith(".toc", StringComparison.OrdinalIgnoreCase)
                        ? prior.Contains("## Title: FrostmourneCastProbe")
                        : prior.Contains("FROSTMOURNE / WoW 3.3.5a");
                    if (!ours)
                        throw new InvalidDataException("Obcy plik dodatku w " + to +
                            "; nie nadpisuje go automatycznie.");
                    string backup = to + ".fmbackup-" + DateTime.UtcNow.ToString("yyyyMMddHHmmssfff");
                    File.Copy(to, backup, false);
                    Write("ETAP addon_backup=PASS file=" + backup);
                }
                if (!File.Exists(to) || Verify.Hash(to) != expected)
                    File.WriteAllBytes(to, bytes);
                if (Verify.Hash(to) != expected)
                    throw new InvalidDataException("Blad weryfikacji SHA256 zainstalowanego dodatku: " + to);
                Write("ETAP addon_file=PASS path=" + to + " sha256=" + expected);
            }
            Write("ETAP addon_install=PASS folder=" + target +
                " kick_trial_requested=" + kickTrial.Checked +
                " game_addon_loaded=NOT_VERIFIED (sprawdz napis LUA AKTYWNE na ekranie gry)");
        }
        void UpdateAsync(bool install) {
            if (updateBusy) return;
            if (install && game != null && !game.HasExited) {
                updateInfo.Text = "Zamknij WoW przed instalacja DLL"; return;
            }
            updateBusy = true;
            string channel = updateChannel.Text;
            updateInfo.Text = "GitHub: sprawdzanie " + channel + (install ? " / instalacja" : "");
            applyUpdate.Enabled = false;
            Save();
            Action<string> safeLog = message => {
                if (!IsDisposed && IsHandleCreated) BeginInvoke((Action)(() => { if (!IsDisposed) Write("UPDATER " + message); }));
            };
            var worker = new BackgroundWorker();
            worker.DoWork += (s,e) => {
                UpdateFeed feed = UpdateClient.Fetch(channel);
                List<UpdatePackage> pending = UpdateClient.Pending(feed, safeLog);
                if (!install) e.Result = "GitHub " + channel + ": " + pending.Count + " aktualizacji";
                else e.Result = UpdateClient.Apply(feed,
                    () => game != null && !game.HasExited, safeLog);
            };
            worker.RunWorkerCompleted += (s,e) => {
                updateBusy = false; applyUpdate.Enabled = true;
                if (e.Error != null) {
                    updateInfo.Text = "GitHub/offline: " + e.Error.Message +
                        " | lokalne zweryfikowane moduly pozostaja dostepne";
                    Write("UPDATER FAIL " + e.Error);
                } else {
                    updateInfo.Text = e.Result.ToString();
                    Write("UPDATER " + updateInfo.Text);
                    if (install) {
                        List<Module> fresh = ModuleCatalog.Discover(Write);
                        if (fresh.Count > 0) {
                            ModuleCatalog.Restore(fresh, Write);
                            selected.Clear(); selected.AddRange(fresh);
                            RefreshModules();
                        }
                    }
                }
            };
            worker.RunWorkerAsync();
        }
        void Launch() {
            if (checking || updateBusy) { Write("Uruchomienie wstrzymane podczas aktualizacji"); return; }
            checking = true;
            try {
                if (game != null && !game.HasExited) { Write("FAIL: wlasny proces Wow juz uruchomiony PID=" + game.Id); return; }
                game = null; pidInfo.Text = "PID: --"; gameInfo.Text = "Gra: KONTROLA";
                resultInfo.Text = "TEST DLL W WOW: NIEPRZETESTOWANE"; dllState = "NIEPRZETESTOWANE";
                if (!VerifyExe() || !VerifyDlls()) {
                    gameInfo.Text = "Gra: FAIL kontroli plikow"; resultInfo.Text = "FAIL: weryfikacja plikow";
                    return;
                }
                // Place the official read-only addon in the game AddOns directory BEFORE launch.
                // This only happens after strict EXE and packaged-DLL checks.
                try {
                    if (selected.Exists(m => m.Enabled && m.Manifest == null)) InstallCastProbeAddon();
                    foreach (Module m in selected.FindAll(x => x.Enabled && x.Manifest != null))
                        ModuleCatalog.InstallAssets(m, System.IO.Path.GetDirectoryName(verifiedExe), kickTrial.Checked, Write);
                } catch (Exception addonError) {
                    gameInfo.Text = "Gra: NIEURUCHOMIONA - instalacja dodatku FAIL";
                    resultInfo.Text = "ADDON FAIL: " + addonError.Message;
                    Write("ETAP addon_install=FAIL " + addonError);
                    return;
                }
                // Verify again immediately before CreateProcess; never change the game's EXE.

                if (Verify.Hash(exe.Text) != verifiedHash) throw new InvalidDataException("Wow.exe zmienil sie przed uruchomieniem");
                ProcessStartInfo start = new ProcessStartInfo {
                    FileName = verifiedExe, WorkingDirectory = System.IO.Path.GetDirectoryName(verifiedExe),
                    UseShellExecute = false
                };
                Write("CreateProcess: start Wow.exe; po weryfikacji PID jeden kontrolowany test natywnego ladowania bootstrap.");
                game = Process.Start(start);
                if (game == null) throw new InvalidOperationException("CreateProcess nie zwrocil procesu");
                pidInfo.Text = "PID: " + game.Id;
                gameInfo.Text = "Gra: URUCHOMIONA (supervisor)";
                resultInfo.Text = "DLL W WOW: PROBA JEDNORAZOWA";
                Write("ETAP weryfikacja_klienta=PASS build=12340 sha256=" + verifiedHash + " pid=" + game.Id);
                Write("ETAP weryfikacja_DLL=" + (selected.Exists(m => m.Enabled) ? "PASS dyskowy_pin_sha256=" + pinnedHash : "POMINIETA wszystkie_DLL_wylaczone") + " pid=" + game.Id);
                Write("ETAP uruchomienie_gry=PASS pid=" + game.Id);
                Write("ETAP mechanizm_rozszerzen=STANDARDOWE_WIN32_LOADLIBRARY pid=" + game.Id);
                List<Module> ordered = selected.Exists(m => m.Enabled && m.Manifest != null)
                    ? ModuleCatalog.Order(selected) : selected.FindAll(m => m.Enabled);
                if (ordered.Count == 0) {
                    resultInfo.Text = "DLL: WYLACZONE; GRA URUCHOMIONA";
                    Write("ETAP zaladowanie_DLL=NIEPRZETESTOWANE przyczyna=wylaczone pid=" + game.Id);
                } else {
                    bool allLoaded = true;
                    foreach (Module module in ordered) {
                        try {
                            if (Verify.Hash(module.Path) != module.Hash ||
                                Verify.Hash(verifiedExe) != verifiedHash)
                                throw new InvalidDataException("EXE/DLL zmienione po weryfikacji");
                            string outcome = (module.Manifest == null || module.Manifest.bootstrap_mode == "legacy_bootstrap")
                                ? RemoteBootstrap.LoadAndInitialize(game, verifiedExe, module.Path,
                                    kickTrial.Checked, (uint)kickWindow.Value, Write)
                                : RemoteBootstrap.LoadGeneric(game, verifiedExe, module.Path,
                                    module.Manifest, module.Options, Write);
                            module.Status = "ZALADOWANY I ZAINICJALIZOWANY w PID " + game.Id +
                                            "; gameplay NIEPOTWIERDZONY";
                            Write("MODULE " + (module.Manifest == null ? "legacy" : module.Id) + " " + outcome);
                        } catch (Exception err) {
                            module.Status = "FAIL in-process: " + err.Message;
                            allLoaded = false;
                            Write("MODULE FAIL " + (module.Manifest == null ? "legacy" : module.Id) + " " + err);
                            break; // Do not load dependants after one failed dependency.
                        } finally { RefreshModules(); }
                    }
                    dllState = allLoaded ? "PASS" : "FAIL";
                    resultInfo.Text = allLoaded
                        ? "DLL: IN_PROCESS_TESTED " + ordered.Count + " | GAMEPLAY NIEPOTWIERDZONY"
                        : "DLL: FAIL / sprawdz logi (gra pozostaje uruchomiona)";
                    dllInfo.Text = "DLL: " + (allLoaded ? "IN_PROCESS_TESTED" : "FAIL") +
                                   " | liczba=" + ordered.Count + " | gameplay: NIEPOTWIERDZONY";
                }
            } catch(Win32Exception ex) {
                gameInfo.Text = "Gra: FAIL CreateProcess Win32=" + ex.NativeErrorCode;
                Write("FAIL CreateProcess: Win32=" + ex.NativeErrorCode + " message=" + ex.Message);
            } catch(Exception ex) {
                gameInfo.Text = "Gra: FAIL " + ex.Message;
                Write("FAIL uruchomienie: " + ex);
            } finally { checking = false; Save(); }
        }
        void PollGame() {
            if (game == null) return;
            try {
                if (game.HasExited) {
                    gameInfo.Text = "Gra: ZAKONCZONA, exit=" + game.ExitCode;
                    Write("EXIT PID=" + game.Id + " code=" + game.ExitCode + "; test DLL=" + dllState);
                    game.Dispose(); game = null; pidInfo.Text = "PID: --";
                }
            } catch(Exception ex) { Write("FAIL odczytu statusu procesu: " + ex.Message); game = null; }
        }
        void Save() {
            try {
                Directory.CreateDirectory(home);
                StringBuilder b = new StringBuilder();
                b.AppendLine("schema=1"); b.AppendLine("exe=" + Verify.Enc(exe.Text));
                b.AppendLine("kicktrial=" + (kickTrial.Checked ? "1" : "0"));
                b.AppendLine("kickwindow=" + ((int)kickWindow.Value).ToString());
                b.AppendLine("updates=" + (autoUpdates.Checked ? "1" : "0"));
                b.AppendLine("channel=" + updateChannel.Text);
                foreach(Module m in selected.Where(m => m.Manifest == null)) b.AppendLine("dll=" + (m.Enabled ? "1" : "0") + "|" + Verify.Enc(m.Path));
                string tmp = settings + ".tmp";
                File.WriteAllText(tmp, b.ToString(), Encoding.UTF8);
                if (File.Exists(settings)) File.Replace(tmp, settings, null);
                else File.Move(tmp, settings);
                ModuleCatalog.Save(selected);
            } catch(Exception ex) { Write("FAIL zapis ustawien: " + ex.Message); }
        }
        void Restore() {
            if (!File.Exists(settings)) return;
            try {
                string[] lines = File.ReadAllLines(settings);
                if (lines.Length == 0 || lines[0].Trim('\uFEFF') != "schema=1") throw new InvalidDataException("Nieznany format konfiguracji");
                foreach(string line in lines) {
                    if (line.StartsWith("exe=")) exe.Text = Verify.Dec(line.Substring(4));
                    if (line == "updates=1") autoUpdates.Checked = true;
                    if (line == "updates=0") autoUpdates.Checked = false;
                    if (line == "channel=stable" || line == "channel=work") updateChannel.SelectedItem = line.Substring(8);
                    if (line == "kicktrial=1") kickTrial.Checked = true;
                    if (line == "kicktrial=0") kickTrial.Checked = false;
                    if (line.StartsWith("kickwindow=")) {
                        int value;
                        if (int.TryParse(line.Substring(11), out value) &&
                            value >= kickWindow.Minimum && value <= kickWindow.Maximum)
                            kickWindow.Value = value;
                    }
                    if (line.StartsWith("dll=")) {
                        int split = line.IndexOf('|');
                        if (split < 5) continue;
                        string path = Verify.Dec(line.Substring(split+1));
                        string bundled = System.IO.Path.GetFullPath(System.IO.Path.Combine(Application.StartupPath, "FrostmourneBootstrap.dll"));
                        // Earlier packages saved manual copies of the same bootstrap from other game folders.
                        // Restore only the package-pinned bootstrap. Never promote or execute an unverified copy.
                        if (String.Equals(System.IO.Path.GetFileName(path), "FrostmourneBootstrap.dll", StringComparison.OrdinalIgnoreCase) &&
                            !String.Equals(System.IO.Path.GetFullPath(path), bundled, StringComparison.OrdinalIgnoreCase)) {
                            migratedLegacyDll = true;
                            Write("MIGRACJA: pominieto zapisany zewnetrzny duplikat FrostmourneBootstrap.dll: " + path +
                                  "; uzywana jest tylko DLL z katalogu loadera i jej manifest SHA256.");
                            continue;
                        }
                        if (!selected.Exists(m => String.Equals(m.Path, path, StringComparison.OrdinalIgnoreCase)))
                            selected.Add(new Module { Path = path, Enabled = line.Substring(4,1) == "1" });
                    }
                }
                Write("Przywrocono ustawienia: " + settings);
            } catch(Exception ex) { Write("FAIL odczyt ustawien: " + ex.Message); }
        }
    }
    internal static class Program {
        [STAThread] static int Main(string[] args) {
            if (args.Length == 1 && args[0] == "--updater-self-test") return UpdateClient.SelfTest() ? 0 : 5;
            if (args.Length == 1 && args[0] == "--self-test") {
                try {
                    string root = Application.StartupPath;
                    string dll = System.IO.Path.Combine(root, "FrostmourneBootstrap.dll");
                    string[] pin = File.ReadAllText(System.IO.Path.Combine(root, "bootstrap.sha256")).Trim().Split(' ');
                    if (pin.Length != 2 || !File.Exists(dll)) return 2;
                    Verify.Pe32(dll, true);
                    if (Verify.Hash(dll) != pin[0].ToLowerInvariant() ||
                        new FileInfo(dll).Length.ToString() != pin[1]) return 3;
                    return 0;
                } catch { return 4; }
            }
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new MainWindow());
            return 0;
        }
    }
}
