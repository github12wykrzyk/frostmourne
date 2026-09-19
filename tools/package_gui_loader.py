"""Build-time validation and exact ZIP for the non-injecting Windows x86 GUI supervisor."""
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


def main() -> int:
    try:
        import pefile
        metadata = json.loads((ROOT / "reference/client/reference.json").read_text(encoding="utf-8"))
        if metadata["sha256"] != REF or metadata["build"] != 12340:
            raise ValueError("reference client fingerprint mismatch")
        source = (ROOT / "src/loader/Program.cs").read_text(encoding="utf-8")
        if "Verify.ReferenceSha" not in source or "Process.Start(start)" not in source:
            raise ValueError("GUI source has no exact client gate or launch path")
        disallowed = ("CreateRemoteThread", "WriteProcessMemory", "VirtualAllocEx",
                      "SetWindowsHookEx", "NtCreateThreadEx", "LoadLibraryW", "OpenProcess(")
        if any(name in source for name in disallowed):
            raise ValueError("GUI contains disallowed process-loading API")
        dll = DIST / "FrostmourneBootstrap.dll"
        exe = DIST / "FrostmourneGui.exe"
        dll_info = audit(dll, dll=True)
        exe_info = audit(exe, dll=False)
        pe = pefile.PE(str(exe))
        try:
            imports = {entry.dll.decode("ascii", "strict").lower()
                       for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", [])}
            if "mscoree.dll" not in imports:
                raise ValueError("GUI executable lacks expected .NET entrypoint")
            clr_directory = pe.OPTIONAL_HEADER.DATA_DIRECTORY[14]
            if not clr_directory.VirtualAddress or not clr_directory.Size:
                raise ValueError("GUI executable lacks CLR header")
            if not pe.DIRECTORY_ENTRY_IMPORT or pe.FILE_HEADER.Machine != 0x14c:
                raise ValueError("GUI is not x86 PE32")
            if any(s.lower() in {"kernel32.dll", "ntdll.dll"} for s in imports):
                raise ValueError("GUI has unexpected direct native Win32 imports")
        finally:
            pe.close()
        pin = f"{dll_info['sha256']} {dll_info['size_bytes']}\n".encode("ascii")
        (DIST / "bootstrap.sha256").write_bytes(pin)
        readme = (ROOT / "docs/GUI_LOADER_TEST.md").read_bytes()
        files = {dll.name: dll_info, debug_dll.name: debug_dll_info,\n                 exe.name: exe_info, debug_exe.name: debug_exe_info,
                 "bootstrap.sha256": {"sha256": sha(pin), "size_bytes": len(pin)},
                 "README-LOADER.md": {"sha256": sha(readme), "size_bytes": len(readme)}}
        manifest = {
            "schema_version": 1,
            "test_kind": "gui-launch-without-injection",
            "client_sha256": REF,
            "process_launch": "Process.Start -> CreateProcess",
            "inprocess_dll_loading": "NOT_AVAILABLE_NO_VERIFIED_EXTENSION_MECHANISM",
            "abi_initialization_in_wow": "NOT_TESTED",
            "active_runtime": False,
            "files": files,
        }
        payloads = {dll.name: dll.read_bytes(), debug_dll.name: debug_dll.read_bytes(),\n                    exe.name: exe.read_bytes(), debug_exe.name: debug_exe.read_bytes(),
                    "bootstrap.sha256": pin, "README-LOADER.md": readme,
                    "test_manifest.json": (json.dumps(manifest, sort_keys=True, indent=2) + "\n").encode("utf-8")}
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
