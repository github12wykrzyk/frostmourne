"""Build one independently updatable, integrity-indexed native x86 module ZIP."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from package_gui_loader import validate_managed_x86
from package_local_bootstrap import audit

REF = "edba72ae4188bda717eec73b733aab9cb2f4ab7d4a1e22b44e60d81743648ebd"
DIST = ROOT / "dist"
MODULE_ID = "frostmourne-bootstrap"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def pack(output: Path, payloads: dict[str, bytes]) -> None:
    index = {"schema_version": 1, "files": {
        path: {"sha256": digest(data), "size_bytes": len(data)}
        for path, data in sorted(payloads.items())
    }}
    contents = dict(payloads)
    contents["package-index.json"] = (json.dumps(index, sort_keys=True, indent=2) + "\n").encode()
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
        for path, data in sorted(contents.items()):
            if path.startswith("/") or ".." in Path(path).parts or "\\" in path:
                raise ValueError("unsafe package path")
            info = zipfile.ZipInfo(path, (1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, data)
    with zipfile.ZipFile(output) as archive:
        if archive.testzip() is not None or set(archive.namelist()) != set(contents):
            raise ValueError("ZIP integrity check failed")
        for name, data in contents.items():
            if archive.read(name) != data:
                raise ValueError(f"ZIP payload mismatch: {name}")


def main() -> None:
    commit = os.getenv("GITHUB_SHA", "local-test")
    version = "0.1.0-work." + commit[:12]
    dll = DIST / "FrostmourneBootstrap.dll"
    details = audit(dll, dll=True)
    if details["sha256"] != digest(dll.read_bytes()):
        raise ValueError("DLL SHA mismatch")
    folder = f"modules/{MODULE_ID}/"
    payloads = {folder + dll.name: dll.read_bytes()}
    assets = []
    for filename in ("FrostmourneCastProbe.lua", "FrostmourneCastProbe.toc"):
        source = ROOT / "src/auto_interrupt/addon" / filename
        payload = source.read_bytes()
        path = "FrostmourneCastProbe/" + filename
        payloads[folder + path] = payload
        assets.append({"path": path, "sha256": digest(payload), "install_path": path})
    manifest = {
        "schema_version": 1, "id": MODULE_ID, "version": version,
        "file": dll.name, "sha256": details["sha256"],
        "client_sha256": REF, "architecture": "x86",
        "abi_major": 1, "abi_minor": 0, "bootstrap_mode": "legacy_bootstrap",
        "enabled_by_default": False, "dependencies": [], "assets": assets,
        "options": []
    }
    payloads[folder + "module.json"] = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
    DIST.mkdir(exist_ok=True)
    output = DIST / "Frostmourne_Modules_x86.zip"
    pack(output, payloads)
    meta = {"id": MODULE_ID, "version": version, "sha256": digest(output.read_bytes()),
            "size_bytes": output.stat().st_size, "client_sha256": REF,
            "architecture": "x86", "abi_major": 1, "abi_minor": 0}
    (DIST / "module-release.json").write_text(json.dumps(meta, indent=2) + "\n")
    print("MODULE ZIP PASS", output, "SHA256", meta["sha256"])


if __name__ == "__main__":
    main()
