from __future__ import annotations
import argparse, subprocess, sys, zipfile
from manifest_common import ROOT, load_json, repo_path

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--output", default="dist/frostmourne-runtime.zip")
    args = ap.parse_args()
    for verifier in ("verify_repo.py", "verify_current.py"):
        r = subprocess.run([sys.executable, str(ROOT / "tools" / verifier)], cwd=ROOT)
        if r.returncode:
            return r.returncode
    manifest = load_json(ROOT / "runtime/current.json")
    out = repo_path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    paths = ["runtime/current.json", "CURRENT.json"]
    paths += [x["path"] for x in manifest.get("files", [])]
    unique = sorted(set(paths))
    with zipfile.ZipFile(out, "w", compression=zipfile.ZIP_DEFLATED) as z:
        for rel in unique:
            data = repo_path(rel).read_bytes()
            zi = zipfile.ZipInfo(rel, date_time=(1980,1,1,0,0,0))
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = 0o644 << 16
            z.writestr(zi, data)
    print(f"PACKAGE: {out.relative_to(ROOT)} files={len(unique)}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
