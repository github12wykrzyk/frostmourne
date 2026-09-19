"""Generate immutable SHA256/size pins for the exact compiled diagnostic DLL."""
from __future__ import annotations
import hashlib
from pathlib import Path

root = Path(__file__).resolve().parents[1]
dll = root / "dist" / "FrostmourneBootstrap.dll"
destination = root / "dist" / "frostmourne_loader_hash.h"
data = dll.read_bytes()
sha = hashlib.sha256(data).hexdigest()
destination.write_text(
    '#pragma once\n'
    '#define FM_EXPECTED_DLL_SHA256 L"' + sha + '"\n'
    '#define FM_EXPECTED_DLL_SIZE ' + str(len(data)) + 'LL\n',
    encoding="ascii",
)
print("PINNED BOOTSTRAP:", len(data), sha)
