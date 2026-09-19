"""Package a read-only Windows x86 Wow.exe identity checker; never an injector."""
from __future__ import annotations

import hashlib
import json
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "dist" / "FrostmourneProcessCheck.exe"
README = ROOT / "docs" / "PROCESS_CHECK.md"
OUT = ROOT / "dist" / "frostmourne-process-check-x86.zip"
EXPECTED_HASH = "edba72ae4188bda717eec73b733aab9cb2f4ab7d4a1e22b44e60d81743648ebd"


def main() -> int:
    try:
        import pefile

        meta = json.loads((ROOT / "reference/client/reference.json").read_text(encoding="utf-8"))
        if meta["sha256"] != EXPECTED_HASH or meta["size_bytes"] != 7704216:
            raise ValueError("reference client fingerprint changed")
        source = (ROOT / "src/bootstrap/process_check.c").read_text(encoding="utf-8")
        if EXPECTED_HASH not in source:
            raise ValueError("process checker is not fingerprint-gated")
        for forbidden in ("CreateRemoteThread", "WriteProcessMemory", "VirtualAllocEx",
                          "AdjustTokenPrivileges", "Frostmourne_Initialize("):
            if forbidden in source.replace("/* Never open the target with PROCESS_VM_WRITE, PROCESS_VM_OPERATION or PROCESS_CREATE_THREAD. */", ""):
                raise ValueError(f"process checker contains disallowed operation: {forbidden}")
        data = EXE.read_bytes()
        pe = pefile.PE(data=data, fast_load=False)
        try:
            if pe.FILE_HEADER.Machine != 0x14C or pe.OPTIONAL_HEADER.Magic != 0x10B:
                raise ValueError("diagnostic EXE is not PE32 x86")
            if pe.FILE_HEADER.Characteristics & 0x2000:
                raise ValueError("diagnostic unexpectedly compiled as a DLL")
            imports = sorted(
                entry.dll.decode("ascii", "strict").lower()
                for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", [])
            )
            if any(d.startswith(("vcruntime", "msvcp", "msvcr", "ucrtbase"))
                   for d in imports):
                raise ValueError(f"unexpected external MSVC runtime dependency: {imports}")
        finally:
            pe.close()
        manifest = {
            "schema_version": 1,
            "test_kind": "wow-process-readonly-identity-and-module-check",
            "architecture": "Windows x86 PE32",
            "reference_client_sha256": EXPECTED_HASH,
            "loads_dll_into_wow": False,
            "confirms_dll_initialization_in_wow": False,
            "files": {
                EXE.name: {
                    "sha256": hashlib.sha256(data).hexdigest(),
                    "size_bytes": len(data),
                    "imports": imports,
                },
                "README-PROCESS-CHECK.md": {
                    "sha256": hashlib.sha256(README.read_bytes()).hexdigest(),
                    "size_bytes": len(README.read_bytes()),
                },
            },
        }
        contents = {
            EXE.name: data,
            "README-PROCESS-CHECK.md": README.read_bytes(),
            "test_manifest.json": (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("utf-8"),
        }
        OUT.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(OUT, "w", compression=zipfile.ZIP_DEFLATED) as z:
            for name, payload in sorted(contents.items()):
                info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o644 << 16
                z.writestr(info, payload)
        with zipfile.ZipFile(OUT) as z:
            if sorted(z.namelist()) != sorted(contents):
                raise ValueError("unexpected ZIP files")
            for name, payload in contents.items():
                if z.read(name) != payload:
                    raise ValueError(f"ZIP integrity mismatch for {name}")
            if "Wow.exe" in z.namelist() or "FrostmourneBootstrap.dll" in z.namelist():
                raise ValueError("nonessential game binary or bootstrap found in ZIP")
        print("PROCESS CHECK PACKAGE: PASS")
        print(f"ZIP={OUT.name} SHA256={hashlib.sha256(OUT.read_bytes()).hexdigest()}")
        print(f"EXE={EXE.name} SHA256={manifest['files'][EXE.name]['sha256']} imports={imports}")
        return 0
    except Exception as exc:
        print(f"PROCESS CHECK PACKAGE: FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
