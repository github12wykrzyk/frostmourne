# FROSTMOURNE DLL loader design and standalone updater

Status: loader, injected DLL, proxy, launcher modification and game modules remain architectural proposals. An independent experimental Windows x86 updater is now implemented under `src/updater/` with unit tests and a CI-built distribution (see `docs/UPDATER.md`). No updater or game EXE/DLL exists in the **active game runtime manifest** yet. The reference EXE is not automatically an active release file.

## System boundaries

[Whitemane launcher] -> [verified running Wow.exe]
                                      ^
[external FROSTMOURNE supervisor] -> [one x86 bootstrap DLL] -> [coherent x86 feature modules]
                 |
       [staged updater + verified release manifest + local rollback store]

Keep executable observation/launch ownership, update orchestration, diagnostics and image verification outside the game. A single minimal in-process bootstrap is responsible for ordered module activation and health reporting; feature DLL boundaries follow ownership and fault domains, not one DLL per tiny option. Release manifest must describe the exact EXE/DLL/config combination as one compatible set.

## Loading choices and selection criteria

| Option | Properties | Status/decision |
| --- | --- | --- |
| External supervisor attaches to the actual, already-running Wow.exe | Avoids editing original EXE or relying on launcher internals; can gate on exact file hash and initialization state. Attaching/standard OS DLL loading requires correct Windows privileges, policy compliance, x86 bitness and explicit diagnostics. | Design candidate; no attachment, injection or remote loading implemented/tested. |
| Supervisor launches target EXE itself | Better sequencing and ownership if launcher permits direct launch; may conflict with Whitemane authentication/updates. | Do not assume viable until authorized launch behavior is checked. |
| Static proxy DLL at a genuine imported library name | DINPUT8.dll appears in the static import table, but proxy must faithfully forward all exports and avoid recursive loading/dependency collisions. Launcher can replace files, and other products may already own the proxy location. | Contingency only after controlled compatibility tests. Do not intercept system DLLs by assumption. |
| Modify on-disk Wow.exe or embed loader in EXE | Requires rebuilding fingerprint and maintaining a separate binary provenance/rollback story for every patch. | Not part of initial architecture. |
| Ordinary in-process LoadLibrary for feature DLLs | Useful **inside** the bootstrap after its own verified initialization; cannot by itself cause an unrelated running process to load an external module. | Supported architectural primitive, not a complete external attachment mechanism. |

Avoid undocumented client code offsets or reliance on an EXE section name as an injection entry point. Do not assume anti-cheat or the publisher permits third-party modules. The system must fail closed when the target, privileges, launcher rules or behavior cannot be verified; do not attempt to circumvent security controls.

## Supervisor lifecycle (proposed)

1. Discover game process by PID and canonical full executable image path, not solely the basename. Distinguish whitemane.exe, launcher-game.exe and the actual Wow.exe. Respect multiple instances and process restarts. Only explicit target PID is eligible for activation.
2. Verify the on-disk executable's SHA256, size, PE32 machine and version against reference/client/reference.json. To eliminate time-of-check/time-of-use ambiguity, recheck loaded image identity with appropriate read-only process diagnostics; if the loaded image cannot be safely matched to expected bytes, block client-dependent modules. Note that file hash alone does not prove memory image equivalence.
3. Verify the process is the required x86 bitness. A 64-bit Windows host is fine; every in-process component remains x86. Resolve required DLLs/CRT from a known private installation directory with an allowlist and pinned SHA256.
4. Wait for an explicitly evidenced readiness signal. Process creation, finding a window, or sleeping a fixed number of milliseconds is not proof the game is ready. Check lifetime/exit concurrently; do not attach to stale/reused PIDs.
5. If permitted and tested, perform a single standard Windows OS DLL load of the bootstrap in the identified target. The exact attachment procedure is a separate implementation task and must be validated in an authorized test environment.
6. Bootstrap entry is minimal. In DllMain avoid thread creation, loader reentry, module loading, waiting for game threads, heap-heavy operations and client callbacks. Start the real module manager at a safe, explicitly controlled point after loader initialization.
7. Module manager reads immutable release configuration, validates dependency DAG and compatibility set, then activates modules in topological order. Each module supplies its stable component ID, ABI version, expected EXE hash, feature version, dependencies, initialization result, shutdown/drain status and health checks. On any failure, stop further activation and unload only modules that report a safe quiescent state.
8. A process-exit/cancel signal halts new callbacks and work, drains registered hooks and workers in reverse dependency order, persists GUI settings and releases resources. Do not force FreeLibrary on code still executing or from DllMain. If full safe unload is impossible, disable functionality and let normal process exit reclaim the module.
9. Keep UI configuration separate from immutable versioned release settings. All configurable features have GUI controls and persist settings atomically under a dedicated writable user configuration location. Validate schema and provide migration/rollback of settings.

No client-specific hook, function address, structural layout or production readiness signal is established by the static PE audit; these require independent dynamic tests before implementation.

## Minimal in-process ABI (design, no source generated yet)

- One bootstrap DLL with a narrow versioned C ABI (extern C, explicit cdecl/stdcall convention, fixed-width fields, no STL objects or CRT allocation across ABI boundary).
- Define bootstrap/module ABI major/minor, feature ID, build ID, target EXE SHA256, dependency IDs, capability flags, initialize, health, quiesce and shutdown hooks.
- All module entry points must report status, never throw exceptions across boundaries, and tolerate failed/partial initialization.
- Modules have an explicit state machine: discovered -> verified -> loaded -> initialized -> active -> quiescing -> stopped; any error transitions to failed and blocks dependents.
- One logging/diagnostic bus with bounded nonblocking queues and module-tagged records. No direct logging or complex work inside DllMain.
- Keep binary linkage and loading order deterministic. No implicit cross-module globals; reject dependency cycles, missing symbols and ABI-major mismatches before activation.

## Updater: release schema and recoverable activation (experimental implementation; production hardening remains)

Each released file record needs component ID, release ID, kind (EXE/DLL/data), path relative to installation root, byte length, SHA256, architecture, source provenance, version, dependency IDs and exact set of compatible reference EXE fingerprints/ABI constraints. A release lock binds all executable modules and configuration schema to one fully specified compatible set. Never rely on name/version strings alone. For EXE deliveries, explicitly mark whether the original reference EXE is *copied unchanged* or an independently built/modified artifact with its own hash and source provenance. Do not silently classify a user-provided binary as canonical editable source.

Proposed transaction states: idle -> plan -> download-to-staging -> verify byte hashes/signature-policy -> validate dependency DAG/compatibility/exact installed EXE -> ensure game process fully exited -> preserve rollback snapshot -> atomic activation -> post-activation validation -> committed; any failed step -> abort/rollback with durable reason.

- Download only files declared by an exact immutable release manifest. Validate path normalization (no absolute/parent traversal, reparse-point escapes or arbitrary user paths), digest, length, architecture and per-file dependency/ABI constraints.
- Never mix independently updated components unless every declared compatibility and dependency constraint passes. A component opt-out must trigger full-set revalidation and may block the update.
- Stage on the installation volume when possible, use flush/fsync-equivalent durability for manifest/journal and per-file writes, and switch only after all downloads have passed verification. Pure multi-file rename is not atomic on Windows; use a journal, backups and recovery on next startup to ensure interrupted updates do not leave a claimed-success partial set. Prefer a directory-level version pointer when launcher semantics allow it.
- Refuse activation while Wow.exe or a dependent launcher has open managed binaries. Detect process/path identity, wait for user-controlled normal exit; do not forcibly terminate the game or replace loaded EXE/DLL files.
- Make launcher-owned files an explicit conflict policy. Detect when Whitemane rewrites Wow.exe or its dependencies; never fight the official updater in a write race. Separate FROSTMOURNE-managed files and pin/reverify actual process image before module activation.
- Keep the previously accepted manifest and complete exact rollback byte set until the new release has passed post-install verification and the user has accepted the build. A rollback records errors, preserves diagnostics and restores an internally consistent set, not a random mix of older DLLs.
- Report: active/expected EXE and DLL SHA256, versions, release ID, manifest ID, failing stage, OS error, process/module list, timestamps, logs, exception code/address if collected with user consent. Minimize sensitive contents, make upload opt-in and redact unrelated private data.

The experimental updater validates manifest dependency cycles and exact file hashes, stages downloads and journals file replacements with one-level rollback. The repository runtime verifier still checks compatibility-set membership but does not enforce ABI ranges. Loaded-image validation, Whitemane launcher coexistence tests, safe module unloading and crash upload are not implemented; do not treat infrastructure or CI PASS as production readiness.

## First implementation gate

- Keep reference/client/Wow.exe and reference/client/pe_audit.json outside runtime/current.json for now.
- Build a minimal x86 bootstrap with no game hooks, verify hash/PE/API/CRT/dependencies and client fingerprint, and test controlled initialize/shutdown against the exact authorized running client.
- Implement external supervisor and read-only identity/health diagnostics before attempting optional OS loading. Establish publisher compatibility and runtime process behavior first.
- Implement update staging/journal/rollback and compatibility validation before distributing multiple coordinated DLL/EXE components.
- Integrate actual x86 module builds, dependency graph tests, artifact hash verification, startup smoke and crash-report schema into CI. Stable promotion remains contingent on user acceptance.
