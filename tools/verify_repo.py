from __future__ import annotations
from manifest_common import ROOT, load_json, repo_path

REQUIRED = [
    "AGENTS.md","AI_START_HERE.md","AI_INDEX.json","CURRENT.json","runtime/current.json",
    "reference/client/reference.json","reference/client/README.md","reference/client/pe_audit.json",
    "docs/CLIENT_PE_AUDIT.md","docs/LOADER_UPDATER_ARCHITECTURE.md","tools/audit_reference_client.py",
    "tools/verify_repo.py","tools/verify_current.py","tools/verify_reference_client.py","tools/make_runtime_package.py",
    "docs/WORKFLOW.md","docs/MANIFESTS.md","docs/BASELINES.md","docs/UPDATER.md",
    "src/updater/core.py","src/updater/app.py","src/updater/launcher.py","updates/work.json",
    ".github/workflows/verify.yml"
]

def main():
    errors = []
    for rel in REQUIRED:
        if not repo_path(rel).is_file():
            errors.append(f"missing required file: {rel}")
    try:
        idx = load_json(ROOT / "AI_INDEX.json")
        cur = load_json(ROOT / "CURRENT.json")
        runtime = load_json(ROOT / "runtime/current.json")
        client = load_json(ROOT / "reference/client/reference.json")
        updater_release = load_json(ROOT / "updates/work.json")
    except Exception as e:
        print(f"REPOSITORY VERIFICATION: FAIL\n - JSON load failed: {e}")
        return 1
    for name, obj in [("AI_INDEX.json", idx), ("CURRENT.json", cur), ("runtime/current.json", runtime), ("reference/client/reference.json", client)]:
        if obj.get("schema_version") != 1:
            errors.append(f"{name}: unsupported schema_version")
    if updater_release.get("schema_version") != 1 or updater_release.get("target_client_sha256") != client.get("sha256"):
        errors.append("updater release schema/client fingerprint mismatch")
    if updater_release.get("files") is None or not isinstance(updater_release.get("files"), list):
        errors.append("updater release files must be an array")
    target = runtime.get("target", {})
    expected = {"product":"World of Warcraft","version":"3.3.5a","build":12340,"platform":"Windows","architecture":"x86"}
    if target != expected:
        errors.append("runtime target must exactly match WoW 3.3.5a build 12340 Windows x86")
    client_target = {k: client.get(k) for k in ("product","version","build","platform","architecture")}
    if client_target != expected:
        errors.append("registered client target must exactly match WoW 3.3.5a build 12340 Windows x86")
    if client.get("sha256") != "edba72ae4188bda717eec73b733aab9cb2f4ab7d4a1e22b44e60d81743648ebd":
        errors.append("registered client SHA256 does not match audited target")
    if client.get("size_bytes") != 7704216:
        errors.append("registered client size does not match audited target")
    if client.get("pe_machine") != "0x014c":
        errors.append("registered client PE machine must be I386 0x014c")
    try:
        pe_report = load_json(ROOT / "reference/client/pe_audit.json")
        if pe_report.get("schema_version") != 1:
            errors.append("reference PE audit: unsupported schema_version")
        if pe_report.get("audited_path") != client.get("repository_path"):
            errors.append("reference PE audit path differs from registered client")
        if pe_report.get("sha256") != client.get("sha256") or pe_report.get("size_bytes") != client.get("size_bytes"):
            errors.append("reference PE audit fingerprint differs from registered client")
        if pe_report.get("coff", {}).get("machine") != "0x0000014c":
            errors.append("reference PE audit machine is not I386")
        versions = pe_report.get("version_resource", {}).get("fixed", [])
        if not any(v.get("file_version") == "3.3.5.12340" for v in versions):
            errors.append("reference PE audit has no 3.3.5.12340 fixed version resource")
    except Exception as e:
        errors.append(f"reference PE audit cannot be read: {e}")
    if cur.get("runtime_manifest") != "runtime/current.json":
        errors.append("CURRENT.json must point to runtime/current.json")
    baseline_path = cur.get("baseline_manifest")
    if not isinstance(baseline_path, str):
        errors.append("CURRENT.json missing baseline_manifest")
    else:
        try:
            bp = repo_path(baseline_path)
            if not bp.is_file():
                errors.append(f"baseline manifest missing: {baseline_path}")
            else:
                bm = load_json(bp)
                if bm.get("baseline_id") != cur.get("baseline_id"):
                    errors.append("baseline id mismatch between CURRENT.json and baseline manifest")
                for rel in bm.get("required_files", []):
                    if not repo_path(rel).is_file():
                        errors.append(f"baseline required file missing: {rel}")
        except Exception as e:
            errors.append(f"baseline error: {e}")
    seen = set()
    for m in idx.get("modules", []):
        mid = m.get("id")
        if not mid or mid in seen:
            errors.append(f"AI_INDEX invalid/duplicate module id: {mid}")
        seen.add(mid)
        for key in ("source", "docs"):
            value = m.get(key)
            if value and not repo_path(value).exists():
                errors.append(f"AI_INDEX module {mid}: missing {key} {value}")
    if errors:
        print("REPOSITORY VERIFICATION: FAIL")
        for e in errors:
            print(" -", e)
        return 1
    print("REPOSITORY VERIFICATION: PASS")
    print(f" baseline={cur['baseline_id']} modules={len(idx.get('modules', []))} runtime_files={len(runtime.get('files', []))}")
    print(f" client_sha256={client['sha256']} client_build={client['build']}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
