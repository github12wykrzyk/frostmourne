"""Validate and package the explicitly experimental local x86 in-process test."""
from __future__ import annotations
import hashlib
import json
import sys
import zipfile
from pathlib import Path

from package_local_bootstrap import audit

ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / "dist"
EXE = DIST / "FrostmourneLocalLoad.exe"
DLL = DIST / "FrostmourneBootstrap.dll"
README = ROOT / "docs" / "LOCAL_INPROCESS_TEST.md"
OUT = DIST / "frostmourne-local-inprocess-x86.zip"
CLIENT = "edba72ae4188bda717eec73b733aab9cb2f4ab7d4a1e22b44e60d81743648ebd"


def main() -> int:
    try:
        from pefile import PE
        client = json.loads((ROOT / "reference/client/reference.json").read_text(encoding="utf-8"))
        if client["sha256"] != CLIENT or client["size_bytes"] != 7704216:
            raise ValueError("target client fingerprint changed")
        source = (ROOT / "src/bootstrap/local_process_loader.c").read_text(encoding="utf-8")
        if '#include "process_check.c"' not in source or "FM_EXPECTED_DLL_SHA256" not in source:
            raise ValueError("loader must reuse the verified process checker and pinned DLL hash")
        files = {
            DLL.name: audit(DLL, dll=True),
            EXE.name: audit(EXE, dll=False),
        }
        header = (DIST / "frostmourne_loader_hash.h").read_text(encoding="ascii")
        if files[DLL.name]["sha256"] not in header or str(files[DLL.name]["size_bytes"]) not in header:
            raise ValueError("loader compile-time DLL pin does not match compiled DLL")
        pe = PE(str(EXE))
        try:
            imports = {e.name.decode("ascii", "ignore") for d in pe.DIRECTORY_ENTRY_IMPORT
                       for e in d.imports if e.name}
            if not {"OpenProcess", "CreateRemoteThread", "WriteProcessMemory",
                    "ReadProcessMemory", "VirtualAllocEx"}.issubset(imports):
                raise ValueError("loader missing required in-process test APIs")
        finally:
            pe.close()
        manifest = {
            "schema_version": 1,
            "test_kind": "experimental-one-shot-isolated-local-wow-inprocess",
            "architecture": "Windows x86 PE32",
            "target_client_sha256": CLIENT,
            "inprocess_success_requires_user_field_evidence": True,
            "active_runtime": False,
            "files": files,
        }
        payloads = {
            DLL.name: DLL.read_bytes(),
            EXE.name: EXE.read_bytes(),
            "README-LOCAL-INPROCESS.md": README.read_bytes(),
            "test_manifest.json": (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode(),
        }
        with zipfile.ZipFile(OUT, "w", compression=zipfile.ZIP_DEFLATED) as z:
            for name, payload in sorted(payloads.items()):
                info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o644 << 16
                z.writestr(info, payload)
        with zipfile.ZipFile(OUT) as z:
            if sorted(z.namelist()) != sorted(payloads):
                raise ValueError("incomplete/unexpected test ZIP")
            for name, payload in payloads.items():
                if z.read(name) != payload:
                    raise ValueError(f"ZIP content mismatch: {name}")
            if "Wow.exe" in z.namelist():
                raise ValueError("do not distribute the target EXE in this test")
        print("LOCAL IN-PROCESS PACKAGE: PASS")
        print(f"ZIP={OUT.name} SHA256={hashlib.sha256(OUT.read_bytes()).hexdigest()}")
        for name, entry in files.items():
            print(f"{name} SHA256={entry['sha256']} size={entry['size_bytes']}")
        return 0
    except Exception as e:
        print(f"LOCAL IN-PROCESS PACKAGE: FAIL: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
