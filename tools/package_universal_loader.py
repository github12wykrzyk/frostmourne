"""Produce independently installable universal-loader and module archives (Windows x86 CI)."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import zipfile

from package_local_bootstrap import audit
from package_gui_loader import validate_managed_x86

ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / "dist"
REF = "edba72ae4188bda717eec73b733aab9cb2f4ab7d4a1e22b44e60d81743648ebd"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def pack(name: str, payloads: dict[str, bytes]) -> Path:
    if any(not p or p.startswith("/") or ".." in Path(p).parts or "\\" in p for p in payloads):
        raise ValueError("unsafe ZIP path")
    index = {
        "schema_version": 1,
        "package": name,
        "files": {p: {"sha256": digest(data), "size_bytes": len(data)}
                  for p, data in sorted(payloads.items())},
    }
    payloads = dict(payloads)
    payloads["package-index.json"] = (json.dumps(index, indent=2, sort_keys=True) + "\n").encode("utf-8")
    output = DIST / name
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path, data in sorted(payloads.items()):
            item = zipfile.ZipInfo(path, (1980, 1, 1, 0, 0, 0))
            item.compress_type = zipfile.ZIP_DEFLATED
            item.external_attr = 0o644 << 16
            archive.writestr(item, data)
    with zipfile.ZipFile(output) as archive:
        if set(archive.namelist()) != set(payloads) or archive.testzip():
            raise ValueError("ZIP entry/CRC validation failed")
        for path, data in payloads.items():
            if archive.read(path) != data:
                raise ValueError("ZIP payload mismatch: " + path)
    print(f"PACKAGE PASS {output.name} sha256={digest(output.read_bytes())}")
    return output


def main() -> None:
    executable = DIST / "FrostmourneGui.exe"
    library = DIST / "FrostmourneBootstrap.dll"
    validate_managed_x86(executable)
    bootstrap = audit(library, dll=True)
    import pefile
    image = pefile.PE(str(library))
    try:
        exports = {s.name for s in image.DIRECTORY_ENTRY_EXPORT.symbols if s.name}
        required = {b"_Frostmourne_Initialize@4", b"_Frostmourne_GetAbi@4",
                    b"_Frostmourne_StartAutoKick@4",
                    b"_Frostmourne_GetAutoPickpocketStatus@4"}
        if not required.issubset(exports):
            raise ValueError("existing bootstrap required ABI exports missing")
    finally:
        image.close()
    loader_files = {
        "FrostmourneGui.exe": executable.read_bytes(),
        "README-LOADER.md": (ROOT / "docs/UNIVERSAL_LOADER.md").read_bytes(),
    }
    pack("Frostmourne_Universal_Loader_x86.zip", loader_files)

    folder = "modules/frostmourne-bootstrap/"
    addon = ROOT / "src/auto_interrupt/addon"
    assets = []
    module_files = {folder + "FrostmourneBootstrap.dll": library.read_bytes()}
    for name in ("FrostmourneCastProbe.toc", "FrostmourneCastProbe.lua"):
        data = (addon / name).read_bytes()
        relative = "FrostmourneCastProbe/" + name
        assets.append({
            "path": relative,
            "sha256": digest(data),
            "install_path": relative,
        })
        module_files[folder + relative] = data
    manifest = {
        "schema_version": 1,
        "id": "frostmourne-bootstrap",
        "version": "0.1.0-experimental",
        "file": "FrostmourneBootstrap.dll",
        "sha256": bootstrap["sha256"],
        "client_sha256": REF,
        "architecture": "x86",
        "abi_major": 1,
        "abi_minor": 0,
        "bootstrap_mode": "legacy_bootstrap",
        "enabled_by_default": False,
        "dependencies": [],
        "assets": assets,
        "options": [],
    }
    module_files[folder + "module.json"] = (
        json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("utf-8")
    pack("Frostmourne_Modules_x86.zip", module_files)
    print("CI boundary: Windows x86 static/ABI/ZIP PASS; in-game initialization and gameplay NOT TESTED")


if __name__ == "__main__":
    main()
