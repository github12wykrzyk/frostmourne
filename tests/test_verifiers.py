import json, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def run(script):
    return subprocess.run([sys.executable, str(ROOT / "tools" / script)], cwd=ROOT, capture_output=True, text=True)

def test_repo_verifier_passes():
    r = run("verify_repo.py")
    assert r.returncode == 0, r.stdout + r.stderr

def test_runtime_verifier_passes_empty_runtime():
    r = run("verify_current.py")
    assert r.returncode == 0, r.stdout + r.stderr

def test_runtime_target_is_exact():
    data = json.loads((ROOT / "runtime/current.json").read_text(encoding="utf-8"))
    assert data["target"] == {"product":"World of Warcraft","version":"3.3.5a","build":12340,"platform":"Windows","architecture":"x86"}
