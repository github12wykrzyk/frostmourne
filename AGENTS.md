# FROSTMOURNE — AI operating rules

Target: World of Warcraft 3.3.5a build 12340, Windows x86.

## Read first
1. `AI_START_HERE.md`
2. `CURRENT.json`
3. `runtime/current.json`
4. `AI_INDEX.json`
5. Only then open files for the module being changed.

## Branch policy
- `main` = user-accepted stable baseline only.
- `work` = all development, experiments and test builds.
- Never develop directly on `main`.
- Promote `work` to `main` only after verification and explicit user acceptance of the tested build.
- Do not create a stable version for every experimental commit.

## Source of truth
- Current source lives under `src/` when game modules exist.
- `runtime/current.json` is the canonical manifest for active runtime files.
- `CURRENT.json` identifies the accepted stable baseline.
- `reference/client/reference.json` identifies the exact audited target client fingerprint used for offsets, hooks and compatibility checks.
- `artifacts/baselines/` stores baseline metadata/recovery material; it is not active source.
- Never infer active code from old commits or artifacts when a canonical source is declared.

## Target client rule
Before the first client-dependent modification, verify the candidate executable with `python tools/verify_reference_client.py --path <Wow.exe>`. Do not transfer offsets, structures, hooks or patches from WoW 1.12 or any other client. The only client binary permitted as a repository reference is the explicitly registered target at `reference/client/Wow.exe`; do not add unrelated or duplicate client binaries.

## Change procedure
1. Inspect the current manifests and relevant indexed module only.
2. Make the smallest coherent change on `work`.
3. Update source, manifests and docs together when required.
4. Run `python tools/verify_repo.py` and `python tools/verify_current.py`.
5. For client-dependent work, also run `python tools/verify_reference_client.py --path <candidate>`.
6. Build affected Windows x86 components when source exists.
7. Package with `python tools/make_runtime_package.py --output dist/frostmourne-runtime.zip`.
8. Record the exact test commit/artifact for the user.
9. After user acceptance, promote the verified commit to `main` and update stable baseline metadata.

## Runtime rules
Every active EXE/DLL entry must include path, component id, version, SHA256, architecture, canonical source and dependencies. Never add placeholder runtime entries. Independent DLL/EXE updates are allowed only when compatibility constraints pass.

## Repository hygiene
Do not duplicate canonical source, edit recovery copies as source, commit secrets/crash dumps/build intermediates, or copy code/binaries from wow112. The registered target client reference is the sole exception for a client executable. If manifests disagree with files, stop promotion and repair the inconsistency.
