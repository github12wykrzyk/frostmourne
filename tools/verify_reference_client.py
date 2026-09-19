from __future__ import annotations
import argparse
import hashlib
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
META_PATH = ROOT / "reference/client/reference.json"

def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()

def pe_machine(path: Path) -> int:
    with path.open("rb") as f:
        if f.read(2) != b"MZ":
            raise ValueError("missing MZ header")
        f.seek(0x3C)
        peoff = struct.unpack("<I", f.read(4))[0]
        f.seek(peoff)
        if f.read(4) != b"PE\0\0":
            raise ValueError("missing PE signature")
        return struct.unpack("<H", f.read(2))[0]

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--path", default=None, help="Client executable to verify. Defaults to repository_path from metadata.")
    ap.add_argument("--metadata-only", action="store_true", help="Validate metadata without requiring the binary to be present.")
    args = ap.parse_args()

    meta = json.loads(META_PATH.read_text(encoding="utf-8"))
    errors = []
    expected = {
        "product": "World of Warcraft",
        "version": "3.3.5a",
        "build": 12340,
        "platform": "Windows",
        "architecture": "x86",
        "pe_machine": "0x014c",
        "size_bytes": 7704216,
        "sha256": "edba72ae4188bda717eec73b733aab9cb2f4ab7d4a1e22b44e60d81743648ebd",
        "file_version_resource": "3, 3, 5, 12340",
    }
    for key, value in expected.items():
        if meta.get(key) != value:
            errors.append(f"metadata {key}: expected {value!r}, got {meta.get(key)!r}")

    rel = meta.get("repository_path")
    if not isinstance(rel, str) or not rel:
        errors.append("metadata repository_path missing")
        path = None
    else:
        path = Path(args.path) if args.path else ROOT / rel

    if not args.metadata_only:
        if path is None or not path.is_file():
            errors.append(f"client binary missing: {path}")
        else:
            if path.stat().st_size != meta["size_bytes"]:
                errors.append(f"size mismatch: {path.stat().st_size}")
            digest = sha256_file(path)
            if digest != meta["sha256"]:
                errors.append(f"sha256 mismatch: {digest}")
            try:
                machine = pe_machine(path)
                if machine != 0x014C:
                    errors.append(f"PE machine mismatch: 0x{machine:04x}")
            except Exception as exc:
                errors.append(f"invalid PE: {exc}")
            data = path.read_bytes()
            if b"3, 3, 5, 12340".decode("ascii").encode("utf-16le") not in data:
                errors.append("file version resource marker 3, 3, 5, 12340 not found")
            if "World of Warcraft".encode("utf-16le") not in data:
                errors.append("World of Warcraft product marker not found")

    if errors:
        print("REFERENCE CLIENT VERIFICATION: FAIL")
        for error in errors:
            print(" -", error)
        return 1

    mode = "metadata-only" if args.metadata_only else str(path)
    print(f"REFERENCE CLIENT VERIFICATION: PASS ({mode})")
    print(f" sha256={meta['sha256']} size={meta['size_bytes']} arch={meta['architecture']} build={meta['build']}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
