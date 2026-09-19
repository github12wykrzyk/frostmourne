import json, subprocess, sys, unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def run(script):
    return subprocess.run([sys.executable, str(ROOT / "tools" / script)], cwd=ROOT, capture_output=True, text=True)

class VerifierTests(unittest.TestCase):
    def test_repo_verifier_passes(self):
        r = run("verify_repo.py")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_runtime_verifier_passes_empty_runtime(self):
        r = run("verify_current.py")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_runtime_target_is_exact(self):
        data = json.loads((ROOT / "runtime/current.json").read_text(encoding="utf-8"))
        self.assertEqual(data["target"], {"product":"World of Warcraft","version":"3.3.5a","build":12340,"platform":"Windows","architecture":"x86"})

if __name__ == "__main__":
    unittest.main()
