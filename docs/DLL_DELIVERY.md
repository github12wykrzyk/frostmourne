# DLL-first delivery contract

## One metric, one real deliverable
Optimize time from user's feature request to the first complete, verified, downloadable Windows x86 package that can perform the requested function on the **exact registered WoW 3.3.5a build 12340**. This objective takes precedence over new documentation, general-purpose frameworks, redundant source rewrites and extending isolated prototypes. Do not skip meaningful verification, claim untested actions, or hide blockers to improve the metric.

## For each gameplay request
- Define one observable in-game success criterion and choose the shortest existing canonical module/loader path.
- Inspect only required modules. Verify reference EXE and actual client compatibility before any offsets, hooks, layouts or native calls. No offsets from WoW 1.12/other builds.
- Implement complete gameplay-facing behavior, including data acquisition, verified action bridge, target/state recheck at the moment of action, lifecycle, GUI and persisted settings when needed. Prefer a minimal vertical slice over a standalone decision engine.
- Compile/link a PE32 x86 DLL as soon as that slice exists. Update its owning GitHub Actions workflow, not just its source. Test unit logic, ABI and bundle integrity; publish one compatible ZIP with loader/DLL/config when required. A gameplay request is not fulfilled by a standalone .exe test or an inert bootstrap DLL.
- Use `verify_current.py` for substantial changes, `verify_repo.py` for repo/baseline/recovery changes and acceptance, and the exact-client verifier for client-dependent changes. Pin SHA256 and dependencies. Preserve baseline until the user accepts the actual tested build.
- Give the user the exact artifact URL/commit, precise in-game test and honest status. Reproduce/fix failures; repeat on `work`.

## Required artifact status
`CORE_ONLY`: tests of independent logic; no in-game action.
`DLL_BUILT`: a real Windows x86 DLL was built and static/package checks passed; no proof of loading in the game.
`IN_PROCESS_TESTED`: recorded success loading/initializing exact DLL in matching game process; no proof of requested gameplay.
`GAMEPLAY_TESTED`: explicit in-game behavior confirmed with recorded test conditions and limits.
`STABLE_ACCEPTED`: user accepted the exact tested build; promote verified commit to `main` and preserve rollback.

Any CI artifact that lacks a verified adapter or real gameplay action MUST declare its actual lower state in module docs, index and package metadata. A green CI job, DLL name, successful `LoadLibrary`, decision engine or non-game reproducer never raises it to `GAMEPLAY_TESTED`.

## CI / GitHub Actions acceptance
The owning module workflow should trigger automatically on its canonical source, integration and packaging changes; compile all affected x86 sources, run checks, verify exact package content/hashes/architecture, and upload a single complete test ZIP with `if-no-files-found: error`. When the module is only a core, publish core-test output as a diagnostic, not a playable build. Extend the existing GUI-loader workflow for integrated DLLs instead of spawning redundant pipelines or DLLs. Keep independent tests only when they reduce iteration time without obstructing integrated delivery.

## Reporting and blockers
Report exact module, code commit, available DLL/ZIP URL, verification result, runtime status, `work`/`main` status and concrete user game test. If there is no verified game bridge, no compatible build environment or no downloadable binary, say which stage is missing and what was actually produced. Do not say a feature is done or offer an invented DLL download. The only user work expected is testing actual prepared builds and accepting a stable version.
