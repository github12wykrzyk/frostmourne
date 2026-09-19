"""Validate and package local-only x86 diagnostic smoke binaries, never a game injector."""
from __future__ import annotations
import hashlib
import json
import struct
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / "dist"
FILES = ("FrostmourneBootstrap.dll", "FrostmourneLocalSmoke.exe")
READ_ME = ROOT / "docs" / "BOOTSTRAP_LOCAL_TEST.md"
CLIENT_SHA = "edba72ae4188bda717eec73b733aab9cb2f4ab7d4a1e22b44e60d81743648ebd"


def audit(path: Path, dll: bool) -> dict:
    import pefile
    data = path.read_bytes()
    if len(data) < 256 or data[:2] != b"MZ":
        raise ValueError(f"{path.name}: not a PE file")
    pe = pefile.PE(data=data, fast_load=False)
    try:
        if pe.FILE_HEADER.Machine != 0x014C or pe.OPTIONAL_HEADER.Magic != 0x10B:
            raise ValueError(f"{path.name}: not Windows x86 PE32")
        if bool(pe.FILE_HEADER.Characteristics & 0x2000) != dll:
            raise ValueError(f"{path.name}: incorrect DLL/EXE characteristics")
        imports = sorted(
            entry.dll.decode("ascii", "strict").lower()
            for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", [])
        )
        if any(dep.startswith(("vcruntime", "msvcp", "msvcr", "ucrtbase")) for dep in imports):
            raise ValueError(f"{path.name}: unexpected external MSVC runtime dependency: {imports}")
        if dll:
            names = {
                sym.name.decode("ascii", "strict")
                for entry in getattr(pe, "DIRECTORY_ENTRY_EXPORT", [])
                for sym in entry.symbols if sym.name
            }
            expected = {
                "_Frostmourne_Initialize@4",
                "_Frostmourne_Shutdown@4",
                "_Frostmourne_GetAbi@4",
            }
            if not expected.issubset(names):
                raise ValueError(f"{path.name}: missing diagnostic ABI exports: {expected-names}")
        return {
            "sha256": hashlib.sha256(data).hexdigest(),
            "size_bytes": len(data),
            "architecture": "x86",
            "imports": imports,
        }
    finally:
        pe.close()


def main() -> int:
    try:
        results = {name: audit(DIST / name, name.lower().endswith(".dll")) for name in FILES}
        manifest = {
            "schema_version": 1,
            "test_kind": "local-process-only-dll-smoke",
            "target": "Windows x86",
            "reference_client_sha256_informational_only": CLIENT_SHA,
            "no_game_process_access": True,
            "files": results,
        }
        payloads = {
            "test_manifest.json": (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("utf-8"),
            "README-LOCAL-TEST.md": READ_ME.read_bytes(),
        }
        payloads.update({name: (DIST / name).read_bytes() for name in FILES})
        out = DIST / "frostmourne-bootstrap-local-x86.zip"
        with zipfile.ZipFile(out, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for name, data in sorted(payloads.items()):
                info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o644 << 16
                archive.writestr(info, data)
        with zipfile.ZipFile(out) as archive:
            if sorted(archive.namelist()) != sorted(payloads):
                raise ValueError("unexpected package contents")
            for name, expected in payloads.items():
                if archive.read(name) != expected:
                    raise ValueError(f"{name}: ZIP integrity mismatch")
        print("BOOTSTRAP LOCAL PACKAGE: PASS")
        print(f" package={out.name} size={out.stat().st_size} sha256={hashlib.sha256(out.read_bytes()).hexdigest()}")
        for name, item in results.items():
            print(f" {name} size={item['size_bytes']} sha256={item['sha256']} imports={item['imports']}")
        return 0
    except Exception as exc:
        print(f"BOOTSTRAP LOCAL PACKAGE: FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
