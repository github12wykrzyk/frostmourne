from __future__ import annotations
import hashlib, json, struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def load_json(path: Path):
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)

def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()

def repo_path(value: str) -> Path:
    p = (ROOT / value).resolve()
    try:
        p.relative_to(ROOT.resolve())
    except ValueError:
        raise ValueError(f"path escapes repository: {value}")
    return p

def pe_machine(path: Path) -> int:
    with path.open("rb") as f:
        if f.read(2) != b"MZ":
            raise ValueError("missing MZ header")
        f.seek(0x3C)
        raw = f.read(4)
        if len(raw) != 4:
            raise ValueError("truncated DOS header")
        off = struct.unpack("<I", raw)[0]
        f.seek(off)
        if f.read(4) != b"PE\0\0":
            raise ValueError("missing PE signature")
        raw = f.read(2)
        if len(raw) != 2:
            raise ValueError("truncated COFF header")
        return struct.unpack("<H", raw)[0]
