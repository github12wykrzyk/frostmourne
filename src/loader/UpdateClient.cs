using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net;
using System.Text;
using System.Web.Script.Serialization;
using System.Windows.Forms;

namespace FrostmourneGui {
    // Immutable GitHub Release ZIPs; the branch feed only selects exact SHA256-pinned assets.
    internal sealed class UpdatePackage {
        public string id { get; set; }
        public string version { get; set; }
        public string url { get; set; }
        public string sha256 { get; set; }
        public long size_bytes { get; set; }
        public string client_sha256 { get; set; }
        public string architecture { get; set; }
        public int abi_major { get; set; }
        public int abi_minor { get; set; }
    }
    internal sealed class UpdateFeed {
        public int schema_version { get; set; }
        public string channel { get; set; }
        public string target_client_sha256 { get; set; }
        public UpdatePackage[] packages { get; set; }
    }
    internal sealed class UpdateState {
        public Dictionary<string,string> hashes { get; set; }
    }
    internal sealed class UpdateIndexFile {
        public string sha256 { get; set; }
        public long size_bytes { get; set; }
    }
    internal sealed class UpdateIndex {
        public int schema_version { get; set; }
        public Dictionary<string,UpdateIndexFile> files { get; set; }
    }
    internal static class UpdateClient {
        const string Repo = "github12wykrzyk/frostmourne";
        const int MaxZip = 32 * 1024 * 1024;
        static readonly JavaScriptSerializer Json = new JavaScriptSerializer { MaxJsonLength = 4 * 1024 * 1024 };
        static string StateFile { get { return Path.Combine(Application.StartupPath, "modules.update-state.json"); } }
        static string Base { get { return ModuleCatalog.BaseDir; } }
        static bool Hex(string value) {
            return value != null && value.Length == 64 && value.All(Uri.IsHexDigit);
        }
        static void Validate(UpdateFeed feed, string channel) {
            if (feed == null || feed.schema_version != 1 || feed.channel != channel ||
                feed.target_client_sha256 != Verify.ReferenceSha || feed.packages == null ||
                feed.packages.Length > 64) throw new InvalidDataException("Niezgodny manifest kanalu");
            var ids = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (UpdatePackage p in feed.packages) {
                if (p == null || String.IsNullOrEmpty(p.id) ||
                    !System.Text.RegularExpressions.Regex.IsMatch(p.id, "^[a-z0-9][a-z0-9._-]{0,63}$") ||
                    !ids.Add(p.id) || String.IsNullOrEmpty(p.version) ||
                    p.client_sha256 != Verify.ReferenceSha || p.architecture != "x86" ||
                    p.abi_major != 1 || p.abi_minor != 0 || !Hex(p.sha256) ||
                    p.size_bytes < 100 || p.size_bytes > MaxZip ||
                    String.IsNullOrEmpty(p.url) || !p.url.StartsWith(
                      "https://github.com/" + Repo + "/releases/download/", StringComparison.Ordinal) ||
                    !p.url.EndsWith(".zip", StringComparison.OrdinalIgnoreCase) ||
                    p.url.Contains("?") || p.url.Contains("#") || p.url.Contains("%"))
                    throw new InvalidDataException("Niezgodny pakiet/URL/SHA/ABI " + (p == null ? "null" : p.id));
            }
        }
        internal static UpdateFeed Fetch(string channel) {
            if (channel != "stable" && channel != "work") throw new InvalidDataException("Nieznany kanal");
            ServicePointManager.SecurityProtocol = (SecurityProtocolType)3072;
            string url = "https://raw.githubusercontent.com/" + Repo + "/work/updates/" + channel + "-feed.json";
            using (WebClient client = new WebClient()) {
                client.Headers.Add(HttpRequestHeader.UserAgent, "FrostmourneUpdater/1.0");
                byte[] bytes = client.DownloadData(url);
                if (bytes.Length > 1024 * 1024) throw new InvalidDataException("Manifest przekracza limit");
                UpdateFeed feed = Json.Deserialize<UpdateFeed>(Encoding.UTF8.GetString(bytes));
                Validate(feed, channel);
                return feed;
            }
        }
        static Dictionary<string,string> ReadState() {
            if (!File.Exists(StateFile)) return new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);
            UpdateState v = Json.Deserialize<UpdateState>(File.ReadAllText(StateFile));
            return v == null || v.hashes == null
                ? new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase)
                : new Dictionary<string,string>(v.hashes, StringComparer.OrdinalIgnoreCase);
        }
        static void SaveState(Dictionary<string,string> values) {
            string tmp = StateFile + ".tmp";
            File.WriteAllText(tmp, Json.Serialize(new UpdateState { hashes = values }), Encoding.UTF8);
            if (File.Exists(StateFile)) File.Replace(tmp, StateFile, null);
            else File.Move(tmp, StateFile);
        }
        internal static List<UpdatePackage> Pending(UpdateFeed feed, Action<string> log) {
            Validate(feed, feed.channel);
            var installed = ReadState();
            List<Module> local = ModuleCatalog.DiscoverAt(Base, log);
            var valid = new HashSet<string>(
                local.Where(m => m.Manifest != null && m.Status.StartsWith("ZWERYFIKOWANY"))
                     .Select(m => m.Id), StringComparer.OrdinalIgnoreCase);
            return feed.packages.Where(p => !valid.Contains(p.id) ||
                !installed.ContainsKey(p.id) ||
                !String.Equals(installed[p.id], p.sha256, StringComparison.OrdinalIgnoreCase)).ToList();
        }
        static byte[] Download(UpdatePackage p) {
            using (WebClient client = new WebClient()) {
                client.Headers.Add(HttpRequestHeader.UserAgent, "FrostmourneUpdater/1.0");
                byte[] bytes = client.DownloadData(p.url);
                if (bytes.LongLength != p.size_bytes || bytes.Length > MaxZip)
                    throw new InvalidDataException("Rozmiar artefaktu " + p.id);
                using (var sha = System.Security.Cryptography.SHA256.Create()) {
                    string hash = BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-", "").ToLowerInvariant();
                    if (!String.Equals(hash, p.sha256, StringComparison.OrdinalIgnoreCase))
                        throw new InvalidDataException("SHA256 pobranego ZIP " + p.id);
                }
                return bytes;
            }
        }
        static string SafeEntry(string root, string name, string id) {
            string prefix = "modules/" + id + "/";
            if (!name.StartsWith(prefix, StringComparison.Ordinal) ||
                name.EndsWith("/", StringComparison.Ordinal) ||
                name.IndexOf('\\') >= 0 || name.Contains(":") || name.IndexOf('\0') >= 0)
                throw new InvalidDataException("Niedozwolona sciezka w ZIP: " + name);
            string relative = name.Substring("modules/".Length);
            if (relative.Split('/').Any(s => s.Length == 0 || s == "." || s == ".."))
                throw new InvalidDataException("Traversal ZIP");
            string destination = Path.GetFullPath(Path.Combine(root, relative.Replace('/', Path.DirectorySeparatorChar)));
            if (!destination.StartsWith(Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) +
                Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("Sciezka poza katalogiem modules");
            return destination;
        }
        static void Extract(byte[] zipBytes, UpdatePackage package, string stage) {
            using (var memory = new MemoryStream(zipBytes))
            using (var zip = new ZipArchive(memory, ZipArchiveMode.Read)) {
                var entries = zip.Entries.Where(e => !e.FullName.EndsWith("/", StringComparison.Ordinal)).ToList();
                if (entries.Count > 128 || entries.Count == 0 ||
                    entries.Select(e => e.FullName).Distinct(StringComparer.OrdinalIgnoreCase).Count() != entries.Count)
                    throw new InvalidDataException("Niepoprawne/zdublowane wpisy ZIP");
                ZipArchiveEntry indexEntry = entries.SingleOrDefault(e => e.FullName == "package-index.json");
                if (indexEntry == null || indexEntry.Length > 256 * 1024)
                    throw new InvalidDataException("Brak indeksu integralnosci ZIP");
                UpdateIndex index;
                using (var reader = new StreamReader(indexEntry.Open(), Encoding.UTF8))
                    index = Json.Deserialize<UpdateIndex>(reader.ReadToEnd());
                if (index == null || index.schema_version != 1 || index.files == null ||
                    index.files.Count != entries.Count - 1)
                    throw new InvalidDataException("Indeks ZIP jest niekompletny");
                long total = 0;
                foreach (var entry in entries) {
                    if (entry == indexEntry) continue;
                    UpdateIndexFile expected;
                    if (!index.files.TryGetValue(entry.FullName, out expected) || expected == null ||
                        !Hex(expected.sha256) || expected.size_bytes != entry.Length ||
                        entry.Length < 0 || entry.Length > MaxZip ||
                        ((entry.ExternalAttributes >> 16) & 0xF000) == 0xA000)
                        throw new InvalidDataException("Niepoprawny plik/indeks ZIP: " + entry.FullName);
                    total += entry.Length;
                    if (total > MaxZip) throw new InvalidDataException("Przekroczono limit rozpakowania");
                    string target = SafeEntry(stage, entry.FullName, package.id);
                    Directory.CreateDirectory(Path.GetDirectoryName(target));
                    using (var source = entry.Open())
                    using (var dest = new FileStream(target, FileMode.CreateNew, FileAccess.Write))
                        source.CopyTo(dest);
                    if (new FileInfo(target).Length != expected.size_bytes ||
                        !String.Equals(Verify.Hash(target), expected.sha256, StringComparison.OrdinalIgnoreCase))
                        throw new InvalidDataException("Bledny SHA256 pliku ZIP: " + entry.FullName);
                }
                string manifest = Path.Combine(stage, package.id, "module.json");
                if (!File.Exists(manifest)) throw new InvalidDataException("Brak module.json");
                ModuleManifest m = Json.Deserialize<ModuleManifest>(File.ReadAllText(manifest));
                if (m == null || m.id != package.id || m.version != package.version ||
                    m.abi_major != package.abi_major || m.abi_minor != package.abi_minor ||
                    m.client_sha256 != package.client_sha256 || m.architecture != package.architecture)
                    throw new InvalidDataException("Modul nie pasuje do manifestu kanalu");
            }
        }
        internal static string Apply(UpdateFeed feed, Func<bool> gameRunning, Action<string> log) {
            Validate(feed, feed.channel);
            List<UpdatePackage> pending = Pending(feed, log);
            if (pending.Count == 0) return "Aktualne: wszystkie moduly zweryfikowane";
            if (gameRunning()) throw new InvalidOperationException("Zamknij gre przed aktualizacja DLL");
            string stage = Base + ".pending-" + Guid.NewGuid().ToString("N");
            string backup = Base + ".rollback";
            Directory.CreateDirectory(stage);
            bool movedOld = false, movedNew = false;
            try {
                if (Directory.Exists(Base)) {
                    foreach (string directory in Directory.GetDirectories(Base)) {
                        string id = Path.GetFileName(directory);
                        if (id.StartsWith(".", StringComparison.Ordinal)) continue;
                        CopyTree(directory, Path.Combine(stage, id));
                    }
                }
                foreach (UpdatePackage p in pending) {
                    log("DOWNLOAD " + p.id + " " + p.version);
                    byte[] zip = Download(p);
                    string target = Path.Combine(stage, p.id);
                    if (Directory.Exists(target)) Directory.Delete(target, true);
                    Extract(zip, p, stage);
                    log("SHA256 PASS " + p.id);
                }
                var staged = ModuleCatalog.DiscoverAt(stage, log);
                if (staged.Any(m => m.Manifest == null ||
                    !m.Status.StartsWith("ZWERYFIKOWANY")) ||
                    feed.packages.Any(p => !staged.Any(m => m.Id == p.id &&
                        m.Manifest.version == p.version)))
                    throw new InvalidDataException("Zestaw po aktualizacji nie przeszedl weryfikacji");
                foreach (Module m in staged) m.Enabled = true;
                ModuleCatalog.Order(staged);
                if (gameRunning()) throw new InvalidOperationException("Gra zostala uruchomiona podczas aktualizacji");
                if (Directory.Exists(backup)) Directory.Delete(backup, true);
                if (Directory.Exists(Base)) { Directory.Move(Base, backup); movedOld = true; }
                Directory.Move(stage, Base); movedNew = true;
                var state = ReadState();
                foreach (UpdatePackage p in pending) state[p.id] = p.sha256;
                SaveState(state);
                log("INSTALL PASS " + pending.Count + " pakietow, rollback=" + backup);
                return "Zainstalowano " + pending.Count + " pakietow; zachowano rollback";
            } catch {
                if (movedNew && Directory.Exists(Base)) Directory.Delete(Base, true);
                if (movedOld && Directory.Exists(backup)) Directory.Move(backup, Base);
                throw;
            } finally {
                if (Directory.Exists(stage)) Directory.Delete(stage, true);
            }
        }
        static void CopyTree(string source, string destination) {
            Directory.CreateDirectory(destination);
            foreach (string path in Directory.GetFiles(source))
                File.Copy(path, Path.Combine(destination, Path.GetFileName(path)), true);
            foreach (string directory in Directory.GetDirectories(source))
                CopyTree(directory, Path.Combine(destination, Path.GetFileName(directory)));
        }
        // CI-only end-to-end test: exercises the public GitHub feed, ZIP download,
        // atomic installation and failure before swap on a deliberately invalid SHA.
        internal static bool IntegrationTest() {
            try {
                if (Directory.Exists(Base) || File.Exists(StateFile)) return false;
                UpdateFeed feed = Fetch("work");
                if (feed.packages.Length == 0) return false;
                string result = Apply(feed, () => false, message => {});
                List<Module> actual = ModuleCatalog.DiscoverAt(Base, message => {});
                if (actual.Count == 0 || actual.Any(m => m.Manifest == null ||
                    !m.Status.StartsWith("ZWERYFIKOWANY"))) return false;
                string dll = actual[0].Path;
                string before = Verify.Hash(dll);
                UpdateFeed invalid = Fetch("work");
                invalid.packages[0].sha256 = new string('0', 64);
                invalid.packages[0].version += "-sha-invalid";
                bool failed = false;
                try { Apply(invalid, () => false, message => {}); }
                catch (InvalidDataException) { failed = true; }
                if (!failed || !File.Exists(dll) || Verify.Hash(dll) != before) return false;
                if (Pending(feed, message => {}).Count != 0) return false;
                return result.Contains("Zainstalowano");
            } catch { return false; }
        }
        internal static bool SelfTest() {
            try {
                var good = new UpdateFeed { schema_version = 1, channel = "work",
                    target_client_sha256 = Verify.ReferenceSha, packages = new UpdatePackage[0] };
                Validate(good, "work");
                try { good.packages = new [] { new UpdatePackage { id = "a", abi_major = 2 } };
                    Validate(good, "work"); return false; }
                catch (InvalidDataException) { }
                try { SafeEntry(Path.GetTempPath(), "modules/a/../evil.dll", "a"); return false; }
                catch (InvalidDataException) { }
                return true;
            } catch { return false; }
        }
    }
}
