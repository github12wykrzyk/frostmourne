"""Transactional updater for the exact FROSTMOURNE WoW 3.3.5a client.

No code injection, launcher patching, or background resident process.
"""
from __future__ import annotations

import csv
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid

OWNER = "github12wykrzyk"
REPO = "frostmourne"
TARGET_SHA256 = "edba72ae4188bda717eec73b733aab9cb2f4ab7d4a1e22b44e60d81743648ebd"
TARGET_SIZE = 7704216
SHA256_CHARS = set("0123456789abcdef")
MANAGED = ".frostmourne"


class UpdateError(Exception):
    pass


def digest(path):
    h = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def atomic_json(path, obj):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    try:
        with open(temporary, "w", encoding="utf-8", newline="\n") as stream:
            json.dump(obj, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def safe_rel(value):
    if not isinstance(value, str) or not value or "\\" in value or ":" in value or "//" in value:
        raise UpdateError("Nieprawidłowa ścieżka pliku.")
    p = PurePosixPath(value)
    if p.is_absolute() or any(part in (".", "..") for part in value.split("/")):
        raise UpdateError("Ścieżka poza katalogiem gry.")
    parts = p.parts
    if not parts or any(not part or part.rstrip(" .") != part for part in parts):
        raise UpdateError("Nieprawidłowa ścieżka Windows.")
    reserved = {"CON", "PRN", "AUX", "NUL"}
    if any(part.upper().split(".")[0] in reserved or
           (part.upper().split(".")[0].startswith(("COM", "LPT")) and
            part.upper().split(".")[0][3:].isdigit()) for part in parts):
        raise UpdateError("Zarezerwowana nazwa Windows.")
    if parts[0].lower() == MANAGED:
        raise UpdateError("Pliki wewnętrzne updatera są chronione.")
    return p


def within_install(root, rel):
    p = safe_rel(rel)
    root = Path(root).resolve(strict=True)
    current = root
    for part in p.parts:
        current = current / part
        if current.is_symlink():
            raise UpdateError("Ścieżka prowadzi przez dowiązanie symboliczne.")
    candidate = root.joinpath(*p.parts)
    if not candidate.resolve(strict=False).is_relative_to(root):
        raise UpdateError("Ścieżka poza katalogiem gry.")
    return candidate


def validate_manifest(manifest):
    if not isinstance(manifest, dict) or manifest.get("schema_version") != 1:
        raise UpdateError("Nieobsługiwany manifest.")
    if manifest.get("target_client_sha256") != TARGET_SHA256:
        raise UpdateError("Manifest nie jest przeznaczony dla tego klienta WoW.")
    files = manifest.get("files")
    if not isinstance(files, list):
        raise UpdateError("Manifest nie zawiera listy plików.")
    release = manifest.get("release_id")
    if files and (not isinstance(release, str) or not release.strip()):
        raise UpdateError("Wydanie nie ma identyfikatora.")
    components, paths = {}, set()
    for f in files:
        if not isinstance(f, dict):
            raise UpdateError("Uszkodzony wpis manifestu.")
        comp = f.get("component")
        if not isinstance(comp, str) or not comp or comp in components:
            raise UpdateError("Nieprawidłowy lub powtórzony identyfikator modułu.")
        components[comp] = f
        rel = f.get("path")
        safe_rel(rel)
        if rel.lower() in paths:
            raise UpdateError("Powtórzona ścieżka pliku.")
        paths.add(rel.lower())
        if f.get("kind") not in ("exe", "dll", "data"):
            raise UpdateError("Nieznany rodzaj pliku.")
        if f.get("kind") in ("exe", "dll") and f.get("arch") != "x86":
            raise UpdateError("Moduł nie jest Windows x86.")
        if f.get("kind") == "data" and f.get("arch") not in ("x86", "any"):
            raise UpdateError("Nieprawidłowa architektura pliku danych.")
        if f.get("kind") == "exe" and rel.lower() != "wow.exe":
            raise UpdateError("Updater obsługuje wyłącznie docelowy Wow.exe.")
        if f.get("kind") == "exe" and f.get("sha256") != TARGET_SHA256:
            raise UpdateError("Modyfikowane EXE wymaga osobnego audytu i nowej polityki zgodności.")
        sha = f.get("sha256")
        if not isinstance(sha, str) or len(sha) != 64 or any(c not in SHA256_CHARS for c in sha):
            raise UpdateError("Nieprawidłowy SHA256.")
        if not isinstance(f.get("size_bytes"), int) or isinstance(f["size_bytes"], bool) or f["size_bytes"] < 0:
            raise UpdateError("Nieprawidłowy rozmiar pliku.")
        if not isinstance(f.get("version"), str) or not f["version"]:
            raise UpdateError("Brak wersji pliku.")
        safe_rel(f.get("source_path"))
        if not isinstance(f.get("depends_on"), list):
            raise UpdateError("Brak deklaracji zależności.")
    visiting, visited = set(), set()

    def visit(comp):
        if comp in visiting:
            raise UpdateError("Cykl zależności modułów.")
        if comp in visited:
            return
        if comp not in components:
            raise UpdateError("Brak wymaganego modułu: " + comp)
        visiting.add(comp)
        for dep in components[comp]["depends_on"]:
            if not isinstance(dep, str):
                raise UpdateError("Nieprawidłowa zależność.")
            visit(dep)
        visiting.remove(comp)
        visited.add(comp)

    for component in components:
        visit(component)
    sets = manifest.get("compatibility_sets", [])
    if not isinstance(sets, list):
        raise UpdateError("Nieprawidłowe zestawy kompatybilności.")
    for group in sets:
        if not isinstance(group, dict) or not isinstance(group.get("components"), list):
            raise UpdateError("Uszkodzony zestaw kompatybilności.")
        for component in group["components"]:
            if component not in components:
                raise UpdateError("Zestaw zawiera nieistniejący moduł.")
    return components


def select_files(manifest, selected):
    components = validate_manifest(manifest)
    if not isinstance(selected, (list, set, tuple)):
        raise UpdateError("Nieprawidłowy wybór modułów.")
    requested = set(selected)
    if not requested.issubset(components):
        raise UpdateError("Nieznany moduł.")
    # Manifest describes one coherent release. Disabled components may remain only
    # if their *installed* hash matches the released file exactly (checked at plan time).
    required = set(requested)
    for group in manifest.get("compatibility_sets", []):
        members = set(group["components"])
        if required & members:
            required.update(members)
    changed = True
    while changed:
        before = len(required)
        for comp in list(required):
            required.update(components[comp]["depends_on"])
        changed = len(required) != before
    return required


class _SafeRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, msg, headers, newurl):
        new = super().redirect_request(request, fp, code, msg, headers, newurl)
        if new and urllib.parse.urlsplit(newurl).hostname != "api.github.com":
            new.remove_header("Authorization")
        return new


class GithubSource:
    def __init__(self, token="", ref="work"):
        if ref not in ("work", "main"):
            raise UpdateError("Nieznany kanał aktualizacji.")
        self.token, self.ref = token.strip(), ref
        self.opener = urllib.request.build_opener(_SafeRedirect())

    def _request(self, relative):
        address = "https://api.github.com/repos/{}/{}/contents/{}?ref={}".format(
            OWNER, REPO, urllib.parse.quote(relative, safe="/"), self.ref)
        headers = {"Accept": "application/vnd.github.raw+json",
                   "User-Agent": "Frostmourne-Updater/0.1", "X-GitHub-Api-Version": "2022-11-28"}
        if self.token:
            headers["Authorization"] = "Bearer " + self.token
        return self.opener.open(urllib.request.Request(address, headers=headers), timeout=45)

    def fetch_manifest(self):
        with self._request("updates/" + self.ref + ".json") as response:
            raw = response.read(2 * 1024 * 1024 + 1)
        if len(raw) > 2 * 1024 * 1024:
            raise UpdateError("Manifest jest zbyt duży.")
        try:
            value = json.loads(raw)
        except (ValueError, UnicodeDecodeError) as exc:
            raise UpdateError("Nie można odczytać manifestu; sprawdź token GitHub.") from exc
        if isinstance(value, dict) and "content" in value:
            raise UpdateError("GitHub zwrócił metadane zamiast surowego manifestu.")
        validate_manifest(value)
        return value

    def download(self, repo_path, output, expected_size):
        safe_rel(repo_path)
        with self._request(repo_path) as response, open(output, "wb") as stream:
            remaining = expected_size
            while True:
                data = response.read(min(1024 * 1024, remaining + 1))
                if not data:
                    break
                remaining -= len(data)
                if remaining < 0:
                    raise UpdateError("Pobrany plik jest większy niż zadeklarowano.")
                stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        if remaining:
            raise UpdateError("Pobrany plik jest niekompletny.")


def game_running(install):
    """Fail closed when process state cannot be established on Windows."""
    if os.name != "nt":
        return False  # unit tests / offline audit; installer UI is Windows-only.
    expected = str(Path(install).resolve() / "Wow.exe").casefold()
    try:
        import ctypes
        from ctypes import wintypes
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        psapi = ctypes.WinDLL("psapi", use_last_error=True)
        pids = (wintypes.DWORD * 8192)()
        count = wintypes.DWORD()
        if not psapi.EnumProcesses(ctypes.byref(pids), ctypes.sizeof(pids), ctypes.byref(count)):
            raise OSError("EnumProcesses")
        PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
        for pid in pids[:count.value // ctypes.sizeof(wintypes.DWORD)]:
            if not pid:
                continue
            process = kernel.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
            if not process:
                # Unknown process is tolerated only if its basename is unavailable.
                continue
            try:
                buf = ctypes.create_unicode_buffer(32768)
                size = wintypes.DWORD(len(buf))
                if kernel.QueryFullProcessImageNameW(process, 0, buf, ctypes.byref(size)):
                    if os.path.normcase(buf.value) == os.path.normcase(expected):
                        return True
            finally:
                kernel.CloseHandle(process)
        return False
    except Exception as exc:
        raise UpdateError("Nie udało się bezpiecznie sprawdzić uruchomionej gry: " + str(exc)) from exc


def check_client(install):
    path = within_install(install, "Wow.exe")
    if not path.is_file() or path.stat().st_size != TARGET_SIZE or digest(path) != TARGET_SHA256:
        raise UpdateError("Wow.exe nie odpowiada zweryfikowanemu klientowi Whitemane 3.3.5a (12340).")
    return path


class Updater:
    def __init__(self, install):
        self.root = Path(install).resolve(strict=True)
        if not self.root.is_dir():
            raise UpdateError("Nie znaleziono katalogu gry.")
        self.state = self.root / MANAGED
        if self.state.is_symlink():
            raise UpdateError("Katalog aktualizacji jest dowiązaniem.")
        self.journal = self.state / "journal.json"
        self.installed = self.state / "installed.json"

    def _quiescent(self):
        if game_running(self.root):
            raise UpdateError("Zamknij grę przed aktualizacją lub rollbackiem.")

    def recover(self):
        if not self.journal.exists():
            return False
        self._quiescent()
        record = json.loads(self.journal.read_text(encoding="utf-8"))
        if record.get("state") not in ("prepared", "activating", "committed"):
            raise UpdateError("Nieznany stan transakcji; nie wolno nadpisywać plików.")
        if record["state"] == "committed":
            self.journal.unlink()
            return False
        self._restore(record)
        self.journal.unlink()
        return True

    def _restore(self, record):
        base = self.state / "backups" / record["transaction"]
        if not base.is_dir() or base.is_symlink():
            raise UpdateError("Brak kopii bezpieczeństwa transakcji.")
        for item in reversed(record["files"]):
            target = within_install(self.root, item["path"])
            previous = base / item["path"]
            if item["existed"]:
                if not previous.is_file():
                    raise UpdateError("Brak kopii pliku: " + item["path"])
                target.parent.mkdir(parents=True, exist_ok=True)
                # Keep the backup intact so interrupted rollback is retryable.
                restore = self.state / ("restore-" + uuid.uuid4().hex)
                shutil.copy2(previous, restore)
                os.replace(restore, target)
            else:
                target.unlink(missing_ok=True)
        old = record.get("old_manifest")
        if old is None:
            self.installed.unlink(missing_ok=True)
        else:
            atomic_json(self.installed, old)

    def plan(self, manifest, selected):
        self.recover()
        check_client(self.root)
        components = validate_manifest(manifest)
        required = select_files(manifest, selected)
        current = None
        if self.installed.is_file():
            current = json.loads(self.installed.read_text(encoding="utf-8"))
        # An opt-out is safe only when every release file excluded by the user is already
        # present byte-for-byte. Do not allow a dependency to be silently omitted.
        for comp, item in components.items():
            if comp not in required:
                target = within_install(self.root, item["path"])
                if not target.is_file() or digest(target) != item["sha256"]:
                    raise UpdateError("Nie można pominąć modułu " + comp + ": lokalny plik jest niezgodny.")
        changed = []
        for comp in required:
            item = components[comp]
            target = within_install(self.root, item["path"])
            if not target.is_file() or digest(target) != item["sha256"]:
                changed.append(item)
        return changed, current

    def apply(self, manifest, selected, source, report=lambda message: None):
        self._quiescent()
        changed, old_manifest = self.plan(manifest, selected)
        if not changed:
            report("Wszystkie pliki wydania są już zgodne.")
            return False
        self.state.mkdir(exist_ok=True)
        if self.journal.exists():
            raise UpdateError("Niedokończona transakcja wymaga odzyskania.")
        txid = uuid.uuid4().hex
        stage = self.state / "stage" / txid
        backup = self.state / "backups" / txid
        stage.mkdir(parents=True)
        backup.mkdir(parents=True)
        record = {"state": "prepared", "transaction": txid, "files": [],
                  "old_manifest": old_manifest, "release_id": manifest["release_id"]}
        try:
            for item in sorted(changed, key=lambda f: f["path"]):
                rel = item["path"]
                report("Pobieranie: " + rel)
                temp = stage / rel
                temp.parent.mkdir(parents=True, exist_ok=True)
                source.download(item["source_path"], temp, item["size_bytes"])
                if temp.stat().st_size != item["size_bytes"] or digest(temp) != item["sha256"]:
                    raise UpdateError("Błędny SHA256: " + rel)
                if item["kind"] in ("exe", "dll"):
                    with open(temp, "rb") as stream:
                        if stream.read(2) != b"MZ":
                            raise UpdateError("Nieprawidłowy plik PE: " + rel)
                        stream.seek(0x3c)
                        peoff = int.from_bytes(stream.read(4), "little")
                        stream.seek(peoff)
                        if stream.read(4) != b"PE\x00\x00" or int.from_bytes(stream.read(2), "little") != 0x14c:
                            raise UpdateError("Plik nie jest Windows x86: " + rel)
            self._quiescent()
            check_client(self.root)
            for item in sorted(changed, key=lambda f: f["path"]):
                target = within_install(self.root, item["path"])
                previous = backup / item["path"]
                existed = target.is_file()
                if target.exists() and not existed:
                    raise UpdateError("Ścieżka docelowa nie jest zwykłym plikiem.")
                if existed:
                    previous.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(target, previous)
                    if digest(previous) != digest(target):
                        raise UpdateError("Nie udało się zweryfikować kopii bezpieczeństwa.")
                record["files"].append({"path": item["path"], "existed": existed})
            atomic_json(self.journal, record)
            record["state"] = "activating"
            atomic_json(self.journal, record)
            for item in sorted(changed, key=lambda f: f["path"]):
                target = within_install(self.root, item["path"])
                target.parent.mkdir(parents=True, exist_ok=True)
                os.replace(stage / item["path"], target)
                report("Zainstalowano: " + item["path"])
            for item in manifest["files"]:
                dest = within_install(self.root, item["path"])
                if not dest.is_file() or digest(dest) != item["sha256"]:
                    raise UpdateError("Kontrola zestawu po instalacji nie powiodła się.")
            check_client(self.root)
            atomic_json(self.installed, manifest)
            record["state"] = "committed"
            atomic_json(self.journal, record)
            atomic_json(self.state / "last-rollback.json", record)
            self.journal.unlink()
            report("Aktualizacja zakończona: " + str(manifest["release_id"]))
            return True
        except Exception:
            if self.journal.exists():
                self._quiescent()
                self.recover()
            raise
        finally:
            shutil.rmtree(stage, ignore_errors=True)

    def rollback(self):
        self._quiescent()
        self.recover()
        backups = self.state / "backups"
        if not backups.is_dir():
            raise UpdateError("Brak dostępnej kopii do rollbacku.")
        candidates = [p for p in backups.iterdir() if p.is_dir() and not p.is_symlink()]
        if not candidates:
            raise UpdateError("Brak dostępnej kopii do rollbacku.")
        # A stable pointer prevents arbitrary selection by directory listing order.
        history = self.state / "last-rollback.json"
        if not history.is_file():
            raise UpdateError("Brak zarejestrowanej kopii do rollbacku.")
        record = json.loads(history.read_text(encoding="utf-8"))
        record["state"] = "activating"
        atomic_json(self.journal, record)
        self._restore(record)
        self.journal.unlink()
        history.unlink()
        return record["release_id"]
