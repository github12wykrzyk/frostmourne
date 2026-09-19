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
- `artifacts/baselines/` stores baseline metadata/recovery material; it is not active source.
- Never infer active code from old commits or artifacts when a canonical source is declared.

## Change procedure
1. Inspect the current manifests and relevant indexed module only.
2. Make the smallest coherent change on `work`.
3. Update source, manifests and docs together when required.
4. Run `python tools/verify_repo.py` and `python tools/verify_current.py`.
5. Build affected Windows x86 components when source exists.
6. Package with `python tools/make_runtime_package.py --output dist/frostmourne-runtime.zip`.
7. Record the exact test commit/artifact for the user.
8. After user acceptance, promote the verified commit to `main` and update stable baseline metadata.

## Runtime rules
Every active EXE/DLL entry must include path, component id, version, SHA256, architecture, canonical source and dependencies. Never add placeholder runtime entries. Independent DLL/EXE updates are allowed only when compatibility constraints pass.

## Repository hygiene
Do not duplicate canonical source, edit recovery copies as source, commit secrets/client files/crash dumps/build intermediates, or copy code/binaries from wow112. If manifests disagree with files, stop promotion and repair the inconsistency.
