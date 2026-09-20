using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.IO;
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
            Size = new Size(1060, 815);
            MinimumSize = new Size(1060, 815);
            StartPosition = FormStartPosition.CenterScreen;
            BackColor = Color.FromArgb(19, 23, 33);
            ForeColor = Color.FromArgb(226, 235, 246);
            Font = new Font("Segoe UI", 9F);
            BuildUi();
            ReadPin();
            Restore();
            AddDefaultDll();
            if (migratedLegacyDll) Save(); // Persist migration so stale bootstrap paths cannot block subsequent launches.
            RefreshModules();
            poll.Interval = 1000;
            poll.Tick += (s, e) => PollGame();
            poll.Start();
            Write("GUI START: kontrolowane x86 LoadLibraryW -> FrostmourneBootstrap ABI; bez hookow i ukrywania.");
            Write("Zgodnosc klienta: 3.3.5a build 12340, PE32 x86, scisly SHA256.");
            FormClosing += (s, e) => { Save(); poll.Stop(); /* Nie zamykaj gry przy zamknieciu GUI. */ };
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
            Button("Usun zaznaczony", 884, 195, 137, 30, (s,e) => RemoveDll());
            modules.Location = new Point(24, 230); modules.Size = new Size(997, 185);
            modules.View = View.Details; modules.FullRowSelect = true; modules.CheckBoxes = true;
            modules.GridLines = true; modules.HideSelection = false;
            modules.BackColor = Color.FromArgb(28,35,48); modules.ForeColor = Color.White;
            modules.Columns.Add("Modul", 185); modules.Columns.Add("Wersja / ABI", 110);
            modules.Columns.Add("Arch", 58); modules.Columns.Add("SHA256", 255);
            modules.Columns.Add("Weryfikacja / inicjalizacja", 365);
            modules.ItemChecked += (s,e) => {
                if (!loading && e.Item.Tag is Module) {
                    ((Module)e.Item.Tag).Enabled = e.Item.Checked;
                    Save();
                }
            };
            Controls.Add(modules);
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
            } catch (Exception ex) { Write("FAIL manifest DLL: " + ex.Message); pinnedHash = ""; }
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
                    // Only the exact bundled DLL has an audited import/export and ABI contract.
                    if (!String.Equals(System.IO.Path.GetFullPath(m.Path),
                          System.IO.Path.GetFullPath(System.IO.Path.Combine(Application.StartupPath, "FrostmourneBootstrap.dll")),
                          StringComparison.OrdinalIgnoreCase))
                        throw new InvalidDataException("Brak zatwierdzonego manifestu ABI/zaleznosci dla zewnetrznej DLL");
                    if (pinnedHash.Length != 64 || m.Hash != pinnedHash || new FileInfo(m.Path).Length != pinnedSize)
                        throw new InvalidDataException("DLL rozni sie od binarki z manifestu SHA256/rozmiar");
                    m.Version = "0.1.0-test1 / ABI 1.0";
                    m.Status = "PASS integralnosc; LOAD: NIEPRZETESTOWANE";
                    Write("PASS DLL " + m.Path + " hash=" + m.Hash + " abi=1.0; load=not-tested");
                } catch(Exception ex) { m.Status = "FAIL: " + ex.Message; ok = false; Write("FAIL DLL " + m.Path + ": " + ex); }
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
                    ListViewItem i = new ListViewItem(System.IO.Path.GetFileName(m.Path));
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
            string target = System.IO.Path.Combine(System.IO.Path.GetDirectoryName(verifiedExe),
                "Interface", "AddOns", "FrostmourneCastProbe");
            Directory.CreateDirectory(target);
            foreach (string fileName in new[] { "FrostmourneCastProbe.toc", "FrostmourneCastProbe.lua" }) {
                string from = System.IO.Path.Combine(src, fileName);
                string to = System.IO.Path.Combine(target, fileName);
                string expected = Verify.Hash(from);
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
                    File.Copy(from, to, true);
                if (Verify.Hash(to) != expected)
                    throw new InvalidDataException("Blad weryfikacji SHA256 zainstalowanego dodatku: " + to);
                Write("ETAP addon_file=PASS path=" + to + " sha256=" + expected);
            }
            Write("ETAP addon_install=PASS folder=" + target +
                " game_addon_loaded=NOT_VERIFIED (sprawdz zielony napis LUA AKTYWNE na ekranie gry)");
        }
        void Launch() {
            if (checking) return;
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
                    InstallCastProbeAddon();
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
                Module bootstrap = selected.Find(m => m.Enabled);
                if (bootstrap == null) {
                    resultInfo.Text = "DLL: WYLACZONA; GRA URUCHOMIONA";
                    Write("ETAP zaladowanie_DLL=NIEPRZETESTOWANE przyczyna=wylaczona pid=" + game.Id);
                } else {
                    try {
                        // The only enabled module may be the package-pinned bootstrap (VerifyDlls).
                        if (Verify.Hash(bootstrap.Path) != pinnedHash ||
                            Verify.Hash(verifiedExe) != verifiedHash)
                            throw new InvalidDataException("Plik zmienil sie pomiedzy weryfikacja a probą ladowania");
                        string outcome = RemoteBootstrap.LoadAndInitialize(game, verifiedExe, bootstrap.Path, Write);
                        dllState = "PASS";
                        bootstrap.Status = "PASS: zaladowana i zainicjalizowana w PID " + game.Id;
                        resultInfo.Text = outcome;
                        dllInfo.Text = "DLL: PASS (adresy) | Addon: SKOPIOWANY, status w grze NIEZNANY | Auto Kick OFF";
                        Write("ETAP test_inprocess=PASS pid=" + game.Id + " sha256=" + bootstrap.Hash);
                    } catch (Exception ex) {
                        dllState = "FAIL";
                        bootstrap.Status = "FAIL in-process: " + ex.Message;
                        resultInfo.Text = "DLL W WOW: FAIL (gra pozostaje uruchomiona)";
                        dllInfo.Text = "DLL w procesie Wow.exe: FAIL / sprawdz log";
                        Win32Exception win = ex as Win32Exception;
                        Write("ETAP test_inprocess=FAIL pid=" + game.Id +
                              " win32=" + (win == null ? "NIE_DOTYCZY" : win.NativeErrorCode.ToString()) +
                              " message=" + ex);
                    } finally { RefreshModules(); }
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
                foreach(Module m in selected) b.AppendLine("dll=" + (m.Enabled ? "1" : "0") + "|" + Verify.Enc(m.Path));
                string tmp = settings + ".tmp";
                File.WriteAllText(tmp, b.ToString(), Encoding.UTF8);
                if (File.Exists(settings)) File.Replace(tmp, settings, null);
                else File.Move(tmp, settings);
            } catch(Exception ex) { Write("FAIL zapis ustawien: " + ex.Message); }
        }
        void Restore() {
            if (!File.Exists(settings)) return;
            try {
                string[] lines = File.ReadAllLines(settings);
                if (lines.Length == 0 || lines[0].Trim('\uFEFF') != "schema=1") throw new InvalidDataException("Nieznany format konfiguracji");
                foreach(string line in lines) {
                    if (line.StartsWith("exe=")) exe.Text = Verify.Dec(line.Substring(4));
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
