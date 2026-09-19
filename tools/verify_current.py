from __future__ import annotations
import re
from manifest_common import ROOT, load_json, repo_path, sha256_file, pe_machine

HEX64 = re.compile(r"^[0-9a-f]{64}$")
I386 = 0x014C

def main():
    errors = []
    try:
        manifest = load_json(ROOT / "runtime/current.json")
    except Exception as e:
        print(f"CURRENT RUNTIME VERIFICATION: FAIL\n - cannot load manifest: {e}")
        return 1
    files = manifest.get("files")
    if not isinstance(files, list):
        print("CURRENT RUNTIME VERIFICATION: FAIL\n - files must be an array")
        return 1
    by_component = {}
    paths = set()
    for i, item in enumerate(files):
        prefix = f"files[{i}]"
        if not isinstance(item, dict):
            errors.append(f"{prefix}: entry must be object")
            continue
        for key in ("component","path","version","sha256","arch","canonical_source","depends_on","kind"):
            if key not in item:
                errors.append(f"{prefix}: missing {key}")
        comp = item.get("component")
        rel = item.get("path")
        if comp in by_component:
            errors.append(f"{prefix}: duplicate component {comp}")
        elif comp:
            by_component[comp] = item
        if rel in paths:
            errors.append(f"{prefix}: duplicate path {rel}")
        elif rel:
            paths.add(rel)
        if not rel:
            continue
        try:
            p = repo_path(rel)
        except Exception as e:
            errors.append(f"{prefix}: {e}")
            continue
        if not p.is_file():
            errors.append(f"{prefix}: runtime file missing: {rel}")
            continue
        digest = item.get("sha256", "")
        if not HEX64.fullmatch(digest):
            errors.append(f"{prefix}: sha256 must be lowercase 64-hex")
        elif sha256_file(p) != digest:
            errors.append(f"{prefix}: sha256 mismatch for {rel}")
        src = item.get("canonical_source")
        if not isinstance(src, str):
            errors.append(f"{prefix}: canonical_source must be path")
        else:
            try:
                if not repo_path(src).exists():
                    errors.append(f"{prefix}: canonical_source missing: {src}")
            except Exception as e:
                errors.append(f"{prefix}: {e}")
        kind = item.get("kind")
        if kind in ("exe", "dll"):
            if item.get("arch") != "x86":
                errors.append(f"{prefix}: PE arch must be x86")
            try:
                machine = pe_machine(p)
                if machine != I386:
                    errors.append(f"{prefix}: PE machine 0x{machine:04x}, expected I386 0x014c")
            except Exception as e:
                errors.append(f"{prefix}: invalid PE: {e}")
        elif kind == "data":
            if item.get("arch") not in ("x86", "any"):
                errors.append(f"{prefix}: data arch must be x86 or any")
        else:
            errors.append(f"{prefix}: unsupported kind {kind}")
        if not isinstance(item.get("depends_on"), list):
            errors.append(f"{prefix}: depends_on must be array")
    for comp, item in by_component.items():
        for dep in item.get("depends_on", []):
            if dep not in by_component:
                errors.append(f"{comp}: missing dependency component {dep}")
            if dep == comp:
                errors.append(f"{comp}: self dependency")
    sets = manifest.get("compatibility_sets", [])
    if not isinstance(sets, list):
        errors.append("compatibility_sets must be array")
    else:
        for s in sets:
            members = s.get("components", []) if isinstance(s, dict) else []
            for comp in members:
                if comp not in by_component:
                    errors.append(f"compatibility set references missing component {comp}")
    if manifest.get("state") == "empty" and files:
        errors.append("state is empty but runtime files are present")
    if manifest.get("state") != "empty" and not files:
        errors.append("non-empty state requires runtime files")
    if errors:
        print("CURRENT RUNTIME VERIFICATION: FAIL")
        for e in errors:
            print(" -", e)
        return 1
    print("CURRENT RUNTIME VERIFICATION: PASS")
    print(f" files={len(files)} state={manifest.get('state')}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
