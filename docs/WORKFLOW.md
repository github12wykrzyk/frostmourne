# Development workflow

User request → inspect current manifests/index → change `work` → verify → build Windows x86 → package ZIP → user tests exact artifact → acceptance → promote exact verified commit to `main` → stable baseline metadata.

## Branches
`work` is development. `main` contains only accepted stable states. A test build is identified by exact commit SHA; a stable version is created only after acceptance.

## CI
`.github/workflows/verify.yml` runs repository/runtime verification on pushes and pull requests to `work` or `main`. A Windows job initializes the MSVC x86 environment and compiles `tests/x86_smoke.c` as a CI-only executable. It proves x86 compilation without pretending to be a game module.

When real components are added, their build commands must be included in the Windows job and their outputs must pass `tools/verify_current.py` before packaging.

## First real module
Create `src/<component>/`, add build metadata, build x86 DLL/EXE, then add one runtime manifest entry with path, SHA256, version, x86 architecture, canonical source and dependencies. Update `AI_INDEX.json`, run both verifiers, package, and test. Do not create a stable baseline until user acceptance.
