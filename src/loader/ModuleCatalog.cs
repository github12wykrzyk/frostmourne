using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Web.Script.Serialization;
using System.Windows.Forms;

namespace FrostmourneGui {
    // A versioned, declarative contract: no per-feature names in this catalog.
    public sealed class ModuleOption {
        public string key { get; set; }
        public string type { get; set; }
        public string label { get; set; }
        public string default_value { get; set; }
        public int min { get; set; }
        public int max { get; set; }
        public string[] choices { get; set; }
    }
    public sealed class ModuleAsset {
        public string path { get; set; }
        public string sha256 { get; set; }
        public string install_path { get; set; }
    }
    public sealed class ModuleManifest {
        public int schema_version { get; set; }
        public string id { get; set; }
        public string version { get; set; }
        public string file { get; set; }
        public string sha256 { get; set; }
        public string client_sha256 { get; set; }
        public string architecture { get; set; }
        public int abi_major { get; set; }
        public int abi_minor { get; set; }
        public string bootstrap_mode { get; set; }
        public string init_export { get; set; }
        public string abi_export { get; set; }
        public string option_export { get; set; }
        public bool enabled_by_default { get; set; }
        public string[] dependencies { get; set; }
        public ModuleOption[] options { get; set; }
        public ModuleAsset[] assets { get; set; }
    }
    internal static class ModuleCatalog {
        internal static string BaseDir { get { return Path.Combine(Application.StartupPath, "modules"); } }
        internal static string Config { get { return Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Frostmourne", "modules.cfg"); } }
        static readonly JavaScriptSerializer Json = new JavaScriptSerializer { MaxJsonLength = 1024 * 1024 };
        static string SafeFile(string root, string relative) {
            if (String.IsNullOrWhiteSpace(relative) || Path.IsPathRooted(relative) ||
                relative.IndexOf(':') >= 0 || relative.IndexOf('\0') >= 0)
                throw new InvalidDataException("Niebezpieczna sciezka manifestu: " + relative);
            string full = Path.GetFullPath(Path.Combine(root, relative.Replace('/', Path.DirectorySeparatorChar)));
            if (!full.StartsWith(Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) +
                    Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("Sciezka poza katalogiem modulu: " + relative);
            return full;
        }
        static bool Hex(string s) {
            if (s == null || s.Length != 64) return false;
            foreach (char c in s) if (!Uri.IsHexDigit(c)) return false;
            return true;
        }
        internal static List<Module> Discover(Action<string> log) {
            List<Module> found = new List<Module>();
            if (!Directory.Exists(BaseDir)) return found;
            HashSet<string> ids = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (string folder in Directory.GetDirectories(BaseDir).OrderBy(x => x, StringComparer.OrdinalIgnoreCase)) {
                string manifestPath = Path.Combine(folder, "module.json");
                if (!File.Exists(manifestPath)) continue;
                Module m = new Module { Path = manifestPath, Enabled = false, Status = "WYKRYTY; niezweryfikowany" };
                try {
                    ModuleManifest spec = Json.Deserialize<ModuleManifest>(File.ReadAllText(manifestPath));
                    if (spec == null || spec.schema_version != 1 || String.IsNullOrEmpty(spec.id) ||
                        !System.Text.RegularExpressions.Regex.IsMatch(spec.id, "^[a-z0-9][a-z0-9._-]{0,63}$") ||
                        !ids.Add(spec.id) || String.IsNullOrEmpty(spec.version) ||
                        spec.architecture != "x86" || spec.client_sha256 != Verify.ReferenceSha ||
                        spec.abi_major != 1 || spec.abi_minor != 0 || !Hex(spec.sha256))
                        throw new InvalidDataException("Niezgodny schemat, ID, ABI, x86 lub fingerprint klienta");
                    m.Manifest = spec;
                    m.Id = spec.id;
                    m.Path = SafeFile(folder, spec.file);
                    m.Version = spec.version + " / ABI 1.0";
                    m.Enabled = spec.enabled_by_default;
                    if (!File.Exists(m.Path)) throw new FileNotFoundException("Brak DLL " + m.Path);
                    Verify.Pe32(m.Path, true);
                    if (!String.Equals(Verify.Hash(m.Path), spec.sha256, StringComparison.OrdinalIgnoreCase))
                        throw new InvalidDataException("SHA256 DLL niezgodne z manifestem");
                    m.Hash = spec.sha256.ToLowerInvariant();
                    m.Arch = "x86";
                    if (spec.dependencies == null) spec.dependencies = new string[0];
                    if (spec.options == null) spec.options = new ModuleOption[0];
                    if (spec.assets == null) spec.assets = new ModuleAsset[0];
                    if (String.IsNullOrEmpty(spec.init_export)) spec.init_export = "_Frostmourne_Initialize@4";
                    if (String.IsNullOrEmpty(spec.abi_export)) spec.abi_export = "_Frostmourne_GetAbi@4";
                    if (String.IsNullOrEmpty(spec.option_export)) spec.option_export = "_Frostmourne_SetOption@4";
                    foreach (ModuleAsset asset in spec.assets) {
                        if (!Hex(asset.sha256) || String.IsNullOrEmpty(asset.install_path))
                            throw new InvalidDataException("Nieprawidlowy manifest pliku dodatkowego");
                        string path = SafeFile(folder, asset.path);
                        if (!File.Exists(path) || !String.Equals(Verify.Hash(path), asset.sha256, StringComparison.OrdinalIgnoreCase))
                            throw new InvalidDataException("Niepoprawny SHA256 pliku dodatkowego " + asset.path);
                    }
                    foreach (ModuleOption option in spec.options) ValidateOption(option, option.default_value);
                    m.Status = "ZWERYFIKOWANY; niezaladowany";
                    log("DISCOVER " + m.Id + " verified " + m.Hash);
                } catch (Exception e) {
                    m.Enabled = false; m.Status = "BLOKADA MANIFESTU: " + e.Message;
                    log("DISCOVER BLOCK " + manifestPath + " " + e.Message);
                }
                found.Add(m);
            }
            return found;
        }
        internal static void ValidateOption(ModuleOption o, string value) {
            if (o == null || String.IsNullOrEmpty(o.key) ||
                !System.Text.RegularExpressions.Regex.IsMatch(o.key, "^[a-zA-Z][a-zA-Z0-9_]{0,63}$"))
                throw new InvalidDataException("Nieprawidlowy identyfikator opcji");
            if (o.type == "bool") {
                if (value != "true" && value != "false") throw new InvalidDataException("Opcja bool " + o.key);
            } else if (o.type == "int") {
                int number;
                if (o.min > o.max || !Int32.TryParse(value, out number) || number < o.min || number > o.max)
                    throw new InvalidDataException("Opcja int poza zakresem " + o.key);
            } else if (o.type == "choice") {
                if (o.choices == null || o.choices.Length == 0 || !o.choices.Contains(value))
                    throw new InvalidDataException("Niepoprawny wybor " + o.key);
            } else throw new InvalidDataException("Nieobslugiwany typ opcji " + o.type);
        }
        internal static List<Module> Order(List<Module> all) {
            Dictionary<string,Module> byId = new Dictionary<string,Module>(StringComparer.OrdinalIgnoreCase);
            foreach (Module m in all.Where(m => m.Enabled)) {
                if (m.Manifest == null) throw new InvalidDataException("Modul bez manifestu: " + m.Path);
                if (byId.ContainsKey(m.Id)) throw new InvalidDataException("Powtorzony ID " + m.Id);
                byId.Add(m.Id,m);
            }
            List<Module> sorted = new List<Module>();
            HashSet<string> visiting = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            HashSet<string> finished = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            Action<Module> visit = null;
            visit = m => {
                if (finished.Contains(m.Id)) return;
                if (!visiting.Add(m.Id)) throw new InvalidDataException("Cykl zaleznosci: " + m.Id);
                foreach (string dep in m.Manifest.dependencies ?? new string[0]) {
                    Module parent;
                    if (!byId.TryGetValue(dep,out parent))
                        throw new InvalidDataException(m.Id + ": brak/wylaczona zaleznosc " + dep);
                    visit(parent);
                }
                visiting.Remove(m.Id); finished.Add(m.Id); sorted.Add(m);
            };
            foreach (Module m in byId.Values) visit(m);
            return sorted;
        }
        internal static void InstallAssets(Module m, string gameDirectory, Action<string> log) {
            foreach (ModuleAsset asset in m.Manifest.assets ?? new ModuleAsset[0]) {
                string targetRoot = Path.Combine(gameDirectory, "Interface", "AddOns");
                string target = SafeFile(targetRoot, asset.install_path);
                string source = SafeFile(Path.GetDirectoryName(m.Path), asset.path);
                Directory.CreateDirectory(Path.GetDirectoryName(target));
                bool exists = File.Exists(target);
                if (exists && !String.Equals(Verify.Hash(target), asset.sha256, StringComparison.OrdinalIgnoreCase)) {
                    // Only migrate known legacy FrostmourneCastProbe files. Other addons and
                    // user-modified files are never silently replaced or deleted.
                    string name = Path.GetFileName(target);
                    bool knownPath = String.Equals(
                        Path.GetFileName(Path.GetDirectoryName(target)), "FrostmourneCastProbe",
                        StringComparison.OrdinalIgnoreCase) &&
                        String.Equals(m.Id, "frostmourne-bootstrap", StringComparison.OrdinalIgnoreCase);
                    string existing = File.ReadAllText(target);
                    bool knownLua = String.Equals(name, "FrostmourneCastProbe.lua", StringComparison.OrdinalIgnoreCase) &&
                        existing.Contains("FROSTMOURNE / WoW 3.3.5a") &&
                        existing.Contains("SLASH_FROSTMOURNECASTPROBE1");
                    bool knownToc = String.Equals(name, "FrostmourneCastProbe.toc", StringComparison.OrdinalIgnoreCase) &&
                        existing.Contains("## Title: FrostmourneCastProbe") &&
                        existing.Contains("FrostmourneCastProbe.lua");
                    if (!knownPath || (!knownLua && !knownToc))
                        throw new InvalidDataException("Obcy lub zmodyfikowany plik dodatku; nie nadpisuje: " + target);
                    string backup = target + ".fmbackup-" + DateTime.UtcNow.ToString("yyyyMMddHHmmssfff");
                    string staged = target + ".fmstage-" + Guid.NewGuid().ToString("N");
                    try {
                        File.Copy(source, staged, false);
                        if (!String.Equals(Verify.Hash(staged), asset.sha256, StringComparison.OrdinalIgnoreCase))
                            throw new InvalidDataException("SHA256 przygotowanego dodatku niezgodne: " + name);
                        // Atomic replacement: File.Replace produces a backup of the exact previous file.
                        File.Replace(staged, target, backup);
                        log("ASSET MIGRATION previous_file_backup=" + backup);
                    } finally {
                        if (File.Exists(staged)) File.Delete(staged);
                    }
                } else if (!exists) {
                    string staged = target + ".fmstage-" + Guid.NewGuid().ToString("N");
                    try {
                        File.Copy(source, staged, false);
                        if (!String.Equals(Verify.Hash(staged), asset.sha256, StringComparison.OrdinalIgnoreCase))
                            throw new InvalidDataException("SHA256 przygotowanego dodatku niezgodne: " + Path.GetFileName(target));
                        File.Move(staged, target);
                    } finally {
                        if (File.Exists(staged)) File.Delete(staged);
                    }
                }
                if (!String.Equals(Verify.Hash(target), asset.sha256, StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException("Nieudana instalacja dodatku " + target);
                log("ASSET INSTALL verified " + target);
            }
        }
        internal static void Save(List<Module> modules) {
            Directory.CreateDirectory(Path.GetDirectoryName(Config));
            List<string> lines = new List<string> { "schema=1" };
            foreach (Module m in modules.Where(m => m.Manifest != null)) {
                lines.Add("module=" + Verify.Enc(m.Id) + "|" + (m.Enabled ? "1" : "0"));
                foreach (KeyValuePair<string,string> pair in m.Options)
                    lines.Add("option=" + Verify.Enc(m.Id) + "|" + Verify.Enc(pair.Key) + "|" + Verify.Enc(pair.Value));
            }
            string temp = Config + ".tmp";
            File.WriteAllLines(temp, lines.ToArray());
            if (File.Exists(Config)) File.Replace(temp, Config, null); else File.Move(temp, Config);
        }
        internal static void Restore(List<Module> modules, Action<string> log) {
            if (!File.Exists(Config)) return;
            foreach (string line in File.ReadAllLines(Config)) {
                try {
                    string[] p = line.Split('|');
                    if (p.Length < 2) continue;
                    if (p[0].StartsWith("module=")) {
                        Module m = modules.Find(x => x.Id == Verify.Dec(p[0].Substring(7)));
                        if (m != null && m.Manifest != null && m.Status.StartsWith("ZWERYFIKOWANY"))
                            m.Enabled = p[1] == "1";
                    } else if (p[0].StartsWith("option=") && p.Length == 3) {
                        Module m = modules.Find(x => x.Id == Verify.Dec(p[0].Substring(7)));
                        if (m == null || m.Manifest == null) continue;
                        string key = Verify.Dec(p[1]), value = Verify.Dec(p[2]);
                        ModuleOption option = Array.Find(m.Manifest.options, x => x.key == key);
                        if (option == null) continue;
                        ValidateOption(option,value); m.Options[key] = value;
                    }
                } catch (Exception e) { log("Nieprawidlowe zapisane ustawienie: " + e.Message); }
            }
        }
    }
}
