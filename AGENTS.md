# FROSTMOURNE — obligatory AI operating rules

## 1. Objective: shortest path to a working DLL
Project priority: **minimize elapsed time from the user's gameplay-feature request to a complete, built, verified Windows x86 DLL and a ready-to-test ZIP for the exact WoW 3.3.5a build 12340 client**. Implement actual behavior, not merely plans, research, prompts, decision engines, or status exports. Preserve startup safety, exact-client compatibility and meaningful verification: speed never justifies pretending a feature works or weakening checks.

The AI owns repository changes, research, source, integration, build, CI, dependency manifests and packaging. Do not ask the user to do GitHub operations or assemble binaries when the available tools can do it. A request to implement/fix a feature means deliver a testable binary in the same task wherever technically possible. If a blocking fact or capability prevents a playable implementation, report the exact missing integration and deliver the furthest truthfully verified artifact; never label an inert DLL as gameplay-ready or invent a download.

## 2. Mandatory reading order at each task
Read from the current GitHub `work` branch in this exact order:
1. `AGENTS.md` (this file)
2. `AI_START_HERE.md`
3. `AI_INDEX.json`
4. `CURRENT.json`
5. `runtime/current.json`
6. Only relevant module source, tests, build workflow, manifests and docs.
Current GitHub is the source of truth, not previous conversations or historical ZIPs. Avoid unnecessary full-repository/archive scans. If bootstrap files are absent during initialization, create them.

## 3. Delivery-first task procedure
1. Identify the user-visible acceptance condition and the shortest existing integration path; inspect/reuse the canonical module and the working GUI loader/bootstrap before proposing new architecture. Do not create a new DLL for a small feature if an appropriate module can host it.
2. Ensure `work` descends from current `main`; preserve divergent work before reconciling. Develop on `work` only. `main` is the last user-accepted stable state; experimental commits do not increment stable versions.
3. For the first client-dependent change, verify the exact active EXE using `python tools/verify_reference_client.py --path <Wow.exe>`, `reference/client/reference.json` and a binary/source audit. Never transplant code, offsets, memory layouts or hooks from WoW 1.12 or another client/build. `src/` is canonical editable source root; if `runtime/current.json` declares a `source_path`/`canonical_source`, use that exact path.
4. Make the smallest end-to-end implementation: real verified game-facing adapter, safe action dispatch, lifecycle/exception handling, integration with existing loader, and GUI plus automatically persisted settings where configurable. An isolated decision engine, DLL export, successful `LoadLibrary`, or test executable is not the finished gameplay feature.
5. **Build immediately after the first coherent implementation.** Use the existing Windows x86 GitHub Actions build; update the relevant workflow to compile/link every affected component and upload a complete, clearly named ZIP. Prefer one-command, incremental, parallelizable build/test paths. Do not spend iterations on documentation or infrastructure unrelated to producing the DLL.
6. Run `python tools/verify_current.py` after substantial changes and `python tools/verify_repo.py` when repo/baseline/recovery metadata changes and before stable promotion. Also run the exact-client verifier for client-dependent changes. Verify module ABI, PE32/I386, SHA256, dependencies, package contents and loader compatibility; repair the cause of any failure rather than weakening validators.
7. Deliver one complete test ZIP with the DLL and the compatible loader/EXE/configuration actually needed. If Wow.exe is included, place it at the ZIP root beside DLLs. Do not overwrite a running game or ship incompatible sets. Provide the GitHub Actions artifact or a verified direct download, commit SHA, actual test results and exact in-game checks. If CI cannot test WoW itself, say so.
8. Iterate on observed game results. Only after explicit user acceptance promote the **exact verified build** to `main`, update `CURRENT.json`, `runtime/current.json`, integrity manifests, baseline and rollback metadata, run both verifiers and synchronize `work`.

## 4. Delivery states — never conflate
- `CORE_ONLY`: logic/unit tests only; no native gameplay action.
- `DLL_BUILT`: Windows x86 DLL built and statically/package-verified; in-game behavior not established.
- `IN_PROCESS_TESTED`: exact DLL loaded/initialized in the intended game process; gameplay feature not necessarily working.
- `GAMEPLAY_TESTED`: user or reproducible in-game test confirms requested behavior, with limitations recorded.
- `STABLE_ACCEPTED`: user accepted the tested artifact; only this state may be promoted to `main`.
Do not mark a feature `GAMEPLAY_TESTED` from CI-only tests or a successful loader log. Preserve true status in module docs, index and artifact metadata.

## 5. Runtime, updater, stability
Each active component needs an unambiguous canonical source, version, SHA256, exact-client compatibility and dependencies. The updater must handle the complete compatible EXE/DLL/configuration set, verify hashes, detect incomplete updates, and roll back safely. Do not overwrite files held open by the game. Never select source by similar DLL names or represent recovered source as original.

Protect game startup: check module order, thread affinity, pointers, hook lifetime, unloading and errors. Diagnose crashes across recently changed dependencies rather than treating rollback as a fix. Keep diagnostics capable of reporting EXE/DLL versions, logs and available exception details. Avoid duplicate DLLs, unnecessary refactoring, unstable unverified hooks or manual GitHub work for the user.

## 6. Communication and definition of done
After changes report: implemented gameplay behavior (or exact blocker), affected module, DLL/ZIP download when actually built, verification and CI/in-game status separately, `work`/`main` state and one concrete game test. `napraw` = diagnose, implement, verify and package; `rozbuduj` = extend without regressions; `buduj`/`daj paczkę` = provide complete artifact; `stabilne`/`akceptuję` = promote only the accepted tested commit. A code-only or core-only outcome is explicitly **unfinished** for a gameplay-feature request. See `docs/DLL_DELIVERY.md` and `docs/WORKFLOW.md`.
