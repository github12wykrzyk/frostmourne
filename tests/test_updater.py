"""Offline updater transaction, path and compatibility tests."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from src.updater.core import (
    TARGET_SHA256, UpdateError, Updater, digest, safe_rel, select_files, validate_manifest,
)


def entry(component, path, payload, dependencies=None):
    return {"component": component, "path": path, "source_path": "updates/payloads/" + path,
            "version": "test-1", "kind": "data", "arch": "any",
            "size_bytes": len(payload), "sha256": hashlib.sha256(payload).hexdigest(),
            "depends_on": dependencies or []}


def manifest(files):
    return {"schema_version": 1, "release_id": "test-build-1",
            "target_client_sha256": TARGET_SHA256, "files": files, "compatibility_sets": []}


class Source:
    def __init__(self, payloads):
        self.payloads = payloads

    def download(self, repo_path, output, expected_size):
        data = self.payloads[repo_path]
        assert len(data) == expected_size
        Path(output).write_bytes(data)


class UpdaterTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root / "Wow.exe").write_bytes(b"mock-wow")
        self.mock_game = patch("src.updater.core.game_running", return_value=False)
        self.mock_game.start()
        self.addCleanup(self.mock_game.stop)
        self.mock_client = patch("src.updater.core.check_client", return_value=self.root / "Wow.exe")
        self.mock_client.start()
        self.addCleanup(self.mock_client.stop)

    def test_reject_unsafe_paths(self):
        for value in ("../evil", "/absolute", "a/../b", "a\\b", "C:/foo", ".frostmourne/file",
                      "a//b", "a/b. ", "CON", "a/NUL.log"):
            with self.subTest(path=value):
                with self.assertRaises(UpdateError):
                    safe_rel(value)

    def test_reject_wrong_client_and_dependency_cycle(self):
        bad = manifest([entry("a", "a.bin", b"a")])
        bad["target_client_sha256"] = "0" * 64
        with self.assertRaises(UpdateError):
            validate_manifest(bad)
        cycle = manifest([entry("a", "a.bin", b"a", ["b"]),
                          entry("b", "b.bin", b"b", ["a"])])
        with self.assertRaises(UpdateError):
            validate_manifest(cycle)

    def test_dependency_is_selected(self):
        value = manifest([entry("a", "a.bin", b"a", ["b"]), entry("b", "b.bin", b"b")])
        self.assertEqual({"a", "b"}, select_files(value, ["a"]))

    def test_no_partial_compatible_subset(self):
        value = manifest([entry("a", "a.bin", b"a"), entry("b", "b.bin", b"b")])
        updater = Updater(self.root)
        with self.assertRaises(UpdateError):
            updater.plan(value, ["a"])

    def test_update_then_rollback(self):
        old = self.root / "settings.cfg"
        old.write_bytes(b"old")
        payload = b"new content"
        value = manifest([entry("settings", "settings.cfg", payload)])
        src = Source({"updates/payloads/settings.cfg": payload})
        updater = Updater(self.root)
        self.assertTrue(updater.apply(value, ["settings"], src))
        self.assertEqual(payload, old.read_bytes())
        self.assertEqual(value["release_id"], json.loads(updater.installed.read_text())["release_id"])
        self.assertEqual("test-build-1", updater.rollback())
        self.assertEqual(b"old", old.read_bytes())
        self.assertFalse(updater.installed.exists())

    def test_update_failure_recovers_installed_bytes(self):
        original = self.root / "a.cfg"
        original.write_bytes(b"old a")
        value = manifest([entry("a", "a.cfg", b"new a"), entry("b", "b.cfg", b"new b")])
        class BadSource(Source):
            def download(self, repo_path, output, expected_size):
                if repo_path.endswith("b.cfg"):
                    Path(output).write_bytes(b"tampered")
                else:
                    super().download(repo_path, output, expected_size)
        updater = Updater(self.root)
        with self.assertRaises(UpdateError):
            updater.apply(value, ["a", "b"], BadSource({
                "updates/payloads/a.cfg": b"new a", "updates/payloads/b.cfg": b"new b"}))
        self.assertEqual(b"old a", original.read_bytes())
        self.assertFalse((self.root / "b.cfg").exists())
        self.assertFalse(updater.journal.exists())

    def test_recover_interrupted_activation(self):
        updater = Updater(self.root)
        (self.root / "settings.cfg").write_bytes(b"bad-new")
        backup = updater.state / "backups" / "testtransaction"
        backup.mkdir(parents=True)
        (backup / "settings.cfg").write_bytes(b"good-old")
        updater.journal.parent.mkdir(parents=True, exist_ok=True)
        updater.journal.write_text(json.dumps({
            "state": "activating", "transaction": "testtransaction",
            "release_id": "test", "old_manifest": None,
            "files": [{"path": "settings.cfg", "existed": True}]}))
        self.assertTrue(updater.recover())
        self.assertEqual(b"good-old", (self.root / "settings.cfg").read_bytes())


if __name__ == "__main__":
    unittest.main()
