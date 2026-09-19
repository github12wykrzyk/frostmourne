"""Regression tests for PE signature and machine validation."""
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from manifest_common import pe_machine


class PeMachineTests(unittest.TestCase):
    def _check(self, signature: bytes):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "sample.exe"
            blob = bytearray(0x90)
            blob[0:2] = b"MZ"
            struct.pack_into("<I", blob, 0x3c, 0x80)
            blob[0x80:0x84] = signature
            struct.pack_into("<H", blob, 0x84, 0x14c)
            path.write_bytes(blob)
            return pe_machine(path)

    def test_valid_null_terminated_pe_signature(self):
        self.assertEqual(self._check(b"PE\0\0"), 0x14c)

    def test_rejects_backslash_text_instead_of_nulls(self):
        with self.assertRaises(ValueError):
            self._check(b"PE\\0")


if __name__ == "__main__":
    unittest.main()
