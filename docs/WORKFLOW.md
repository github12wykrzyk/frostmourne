# FROSTMOURNE — development and binary delivery workflow

## Priority
Optimize **time from the user's gameplay request to a complete verified Windows x86 gameplay DLL ZIP** for the registered WoW 3.3.5a build 12340 client. Follow `AGENTS.md` and `docs/DLL_DELIVERY.md`. Architecture, research, isolated engines and diagnostics are stepping stones, not the final gameplay deliverable. Never misreport gameplay readiness.

## Fast path (each iteration)
User-observable behavior → read `AGENTS.md`, `AI_START_HERE.md`, `AI_INDEX.json`, `CURRENT.json`, `runtime/current.json` → relevant module and loader source → confirm `work` follows `main` → verify exact client if client-dependent → smallest real adapter and GUI/config integration → compile x86 DLL immediately → validate PE/ABI/dependencies/SHA256 and package → publish GitHub Actions ZIP → user tests the exact build → fix on `work`. Acceptance of exact verified build → update baseline/rollback and promote to `main` → synchronize `work`.

Avoid unnecessary full scans, unrelated rewrites, extra DLLs and manual user steps. If the module is only an isolated engine, record `CORE_ONLY`; deliver a core diagnostic only, never represent it as playable. See `docs/DLL_DELIVERY.md` for all status labels and definition of done.

## Branches and manifests
`work` is all development and test builds; `main` is last user-accepted stable baseline. Identify tests by commit SHA, not by a newly invented stable version. `CURRENT.json` identifies accepted baseline; `runtime/current.json` contains actual active runtime only. An experimental compiled bootstrap does not automatically become an active runtime module. Updates must pin executable and DLL compatibility and support rollback.

## Registered target client
The audited reference is `reference/client/Wow.exe` and `reference/client/reference.json`, SHA256 `edba72ae4188bda717eec73b733aab9cb2f4ab7d4a1e22b44e60d81743648ebd`, size 7,704,216, PE I386/x86, version 3.3.5.12340. Before deriving addresses, hooks, layouts or patches, run `python tools/verify_reference_client.py --path reference/client/Wow.exe`; check the real on-disk active EXE before assuming its compatibility. A different fingerprint requires an independent audit. Do not copy client-specific code/offsets from other WoW versions.

## Build and CI
`.github/workflows/verify.yml` verifies repository/runtime/client state, runs unit tests, compiles an x86 smoke executable and packages the active runtime manifest. This smoke executable is NOT a gameplay module; the runtime manifest may remain empty.

`.github/workflows/gui-loader-x86.yml` builds the experimental GUI loader and bootstrap DLL, verifies native/managed x86 PE, ABI, SHA256 and ZIP integrity, runs no-game self-tests, and uploads `frostmourne-gui-loader-x86`. Its current bootstrap integrates only the inert Auto Pickpocket decision core. Its successful CI run confirms a compiled test package, not Auto Pickpocket or Auto Interrupt gameplay. For a real gameplay module, update the relevant owning build workflow and existing loader/compatibility manifest to compile/link the verified game adapter, build a complete integrated ZIP and publish a truthful test status.

`.github/workflows/auto-interrupt-engine-x86.yml` and `auto-pickpocket-core.yml` independently test isolated logic. They are diagnostic steps, not replacement for an integrated DLL artifact.

For significant changes run `python tools/verify_current.py`. Also run `python tools/verify_repo.py` for changes to repo/baseline/recovery metadata and stable promotion; run the client verifier for client-dependent work. Recheck architecture, dependencies, SHA256, the ZIP file list and whether the binary matches the loader's allowlist. Fail on missing required artifacts; fix real verifier errors rather than weakening checks.

## In-process and in-game tests
Successful process launch != DLL loaded; DLL loaded != initialized; initialization != gameplay action. Record each independently. GitHub-hosted CI cannot confirm user game actions; only an actual reproduced in-game result with its test conditions may be marked `GAMEPLAY_TESTED`. If no playable implementation is possible, give the user a precise blocker and honest partial state, not a fabricated ZIP or test claim.

## Release
Only after explicit user acceptance of the tested build: promote its exact verified commit to `main`, update stable `CURRENT.json`, `runtime/current.json`, SHA256/dependency manifests, rollback metadata and docs; run both repository and runtime verifiers, synchronize `work`. Never promote based solely on a unit-test or loader status PASS.
