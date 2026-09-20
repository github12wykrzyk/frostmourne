"""Build-time validation and exact ZIP for the experimental Windows x86 in-process loader."""
from __future__ import annotations
import hashlib
import json
import sys
import zipfile
from pathlib import Path

from package_local_bootstrap import audit

ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / "dist"
OUT = DIST / "frostmourne-gui-loader-x86.zip"
REF = "edba72ae4188bda717eec73b733aab9cb2f4ab7d4a1e22b44e60d81743648ebd"


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def validate_managed_x86(path: Path) -> dict:
    import pefile
    info = audit(path, dll=False)
    pe = pefile.PE(str(path))
    try:
        imports = {entry.dll.decode("ascii", "strict").lower()
                   for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", [])}
        if "mscoree.dll" not in imports:
            raise ValueError(f"{path.name}: missing expected .NET entrypoint")
        clr_directory = pe.OPTIONAL_HEADER.DATA_DIRECTORY[14]
        if not clr_directory.VirtualAddress or not clr_directory.Size:
            raise ValueError(f"{path.name}: missing CLR header")
        if pe.FILE_HEADER.Machine != 0x14c:
            raise ValueError(f"{path.name}: not x86 PE32")
        if any(s in {"kernel32.dll", "ntdll.dll"} for s in imports):
            raise ValueError(f"{path.name}: unexpected direct native Win32 imports")
    finally:
        pe.close()
    return info


def main() -> int:
    try:
        metadata = json.loads((ROOT / "reference/client/reference.json").read_text(encoding="utf-8"))
        if metadata["sha256"] != REF or metadata["build"] != 12340:
            raise ValueError("reference client fingerprint mismatch")
        source = (ROOT / "src/loader/Program.cs").read_text(encoding="utf-8")
        if "Verify.ReferenceSha" not in source or "Process.Start(start)" not in source:
            raise ValueError("GUI source has no exact client gate or launch path")
        for marker in ("InstallCastProbeAddon();", "ETAP addon_install=PASS",
                       '"Interface", "AddOns", "FrostmourneCastProbe"',
                       "Verify.Hash(to) != expected", "kickTrial.Checked",
                       'payload.Replace("local kickRequests = false", "local kickRequests = true")'):
            if marker not in source:
                raise ValueError("GUI source lacks verified addon installation: " + marker)
        remote = (ROOT / "src/loader/RemoteBootstrap.cs").read_text(encoding="utf-8")
        expected = ("CreateRemoteThread", "WriteProcessMemory", "VirtualAllocEx",
                    "LoadLibraryW", "Frostmourne_Initialize", "Frostmourne_GetAbi",
                    "GetExitCodeThread", "observed_pid",
                    "Frostmourne_GetAutoPickpocketStatus", "auto_pickpocket_native=PASS",
                    "Frostmourne_ProbeInterruptBindings", "Frostmourne_StartAutoKick",
                    "hook_installed", "kick_trial_enabled")
        if not all(name in remote for name in expected):
            raise ValueError("GUI missing required explicit in-process loading/ABI diagnostics")
        disallowed = ("SetWindowsHookEx", "NtCreateThreadEx", "PROCESS_ALL_ACCESS")
        if any(name in source + remote for name in disallowed):
            raise ValueError("GUI contains unauthorized or overbroad process APIs")

        dll = DIST / "FrostmourneBootstrap.dll"
        debug_dll = DIST / "FrostmourneBootstrapDebug.dll"
        exe = DIST / "FrostmourneGui.exe"
        debug_exe = DIST / "FrostmourneGuiDebug.exe"

        dll_info = audit(dll, dll=True)
        debug_dll_info = audit(debug_dll, dll=True)
        for candidate in (dll, debug_dll):
            import pefile
            image = pefile.PE(str(candidate))
            try:
                exports = {symbol.name for symbol in image.DIRECTORY_ENTRY_EXPORT.symbols if symbol.name}
                if b"_Frostmourne_StartAutoKick@4" not in exports:
                    raise ValueError(candidate.name + ": experimental Kick bridge export missing")
            finally:
                image.close()
        exe_info = validate_managed_x86(exe)
        debug_exe_info = validate_managed_x86(debug_exe)

        pin = f"{dll_info['sha256']} {dll_info['size_bytes']}\n".encode("ascii")
        (DIST / "bootstrap.sha256").write_bytes(pin)
        readme = (ROOT / "docs/GUI_LOADER_TEST.md").read_bytes()

        files = {
            dll.name: dll_info,
            debug_dll.name: debug_dll_info,
            exe.name: exe_info,
            debug_exe.name: debug_exe_info,
            "bootstrap.sha256": {"sha256": sha(pin), "size_bytes": len(pin)},
            "README-LOADER.md": {"sha256": sha(readme), "size_bytes": len(readme)},
        }
        payloads = {
            dll.name: dll.read_bytes(),
            debug_dll.name: debug_dll.read_bytes(),
            exe.name: exe.read_bytes(),
            debug_exe.name: debug_exe.read_bytes(),
            "bootstrap.sha256": pin,
            "README-LOADER.md": readme,
        }
        for addon_name in ("FrostmourneCastProbe.toc", "FrostmourneCastProbe.lua"):
            addon_path = ROOT / "src" / "auto_interrupt" / "addon" / addon_name
            addon_data = addon_path.read_bytes()
            if addon_name.endswith(".lua") and (b"CastSpellByID(" in addon_data or b"CastSpellByName(" in addon_data):
                raise ValueError("diagnostic addon must not issue cast commands")
            name = "FrostmourneCastProbe/" + addon_name
            payloads[name] = addon_data
            files[name] = {"sha256": sha(addon_data), "size_bytes": len(addon_data)}
        addon_lua = payloads["FrostmourneCastProbe/FrostmourneCastProbe.lua"]
        if b"local kickRequests = false" not in addon_lua or b"FMKICK12340|" not in addon_lua:
            raise ValueError("addon lacks guarded opt-in native Kick marker bridge")
        for marker in (b"local running = true", b'frame:RegisterEvent("PLAYER_LOGIN")',
                       b'frame:RegisterEvent("PLAYER_ENTERING_WORLD")',
                       b"FM CAST PROBE: LUA AKTYWNE", b"LUA ADDON ZALADOWANY",
                       b"Auto Kick OFF"):
            if marker not in addon_lua:
                raise ValueError("addon does not auto-start/announce load in game: " + marker.decode("ascii"))

        for optional in ("FrostmourneGuiDebug.pdb", "FrostmourneBootstrapDebug.pdb"):
            q = DIST / optional
            if q.is_file():
                data = q.read_bytes()
                payloads[optional] = data
                files[optional] = {"sha256": sha(data), "size_bytes": len(data)}
        manifest = {
            "schema_version": 1,
            "test_kind": "gui-one-shot-inprocess-bootstrap-attempt",
            "client_sha256": REF,
            "process_launch": "Process.Start -> CreateProcess",
            "inprocess_dll_loading": "IMPLEMENTED_STANDARD_WIN32_NOT_TESTED_ON_GAME_IN_CI",
            "abi_initialization_in_wow": "NOT_TESTED_ON_GAME_IN_CI",
            "auto_pickpocket_core": "COMPILED_WITH_EXPERIMENTAL_NATIVE_SELECTED_TARGET_ADAPTER",
            "auto_pickpocket_gameplay": "NATIVE_CAST_REQUEST_IMPLEMENTED_UNTESTED_IN_GAME_NO_SERVER_LOOT_CONFIRMATION",
            "auto_interrupt_bindings": "INPROCESS_READONLY_REGISTRATION_AND_PROLOGUE_PROBE",
            "auto_interrupt_live_cast": "LUA_CAST_API_AND_NATIVE_FRESH_TARGET_RECHECK_WHEN_OPTED_IN",
            "auto_interrupt_kick": "EXPERIMENTAL_NATIVE_REQUEST_DEFAULT_OFF_NOT_GAMEPLAY_TESTED",
            "active_runtime": False,
            "files": files,
        }
        payloads["test_manifest.json"] = (json.dumps(manifest, sort_keys=True, indent=2) + "\n").encode("utf-8")

        if "Wow.exe" in payloads or any("LocalLoad" in f for f in payloads):
            raise ValueError("forbidden legacy executable in package")
        with zipfile.ZipFile(OUT, "w", compression=zipfile.ZIP_DEFLATED) as z:
            for name, data in sorted(payloads.items()):
                info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o644 << 16
                z.writestr(info, data)
        with zipfile.ZipFile(OUT) as z:
            if set(z.namelist()) != set(payloads):
                raise ValueError("archive missing or unexpected files")
            if z.testzip() is not None:
                raise ValueError("archive CRC check failed")
            for name, expected in payloads.items():
                if z.read(name) != expected:
                    raise ValueError(f"archive content mismatch: {name}")
        print("GUI LOADER PACKAGE: PASS")
        print(f"ZIP={OUT.name} sha256={sha(OUT.read_bytes())} size={OUT.stat().st_size}")
        for name, record in files.items():
            print(f"{name}: size={record['size_bytes']} sha256={record['sha256']}")
        print("game in-process DLL test: NOT TESTED; CI does not start Wow.exe")
        return 0
    except Exception as exc:
        print("GUI LOADER PACKAGE: FAIL:", exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
