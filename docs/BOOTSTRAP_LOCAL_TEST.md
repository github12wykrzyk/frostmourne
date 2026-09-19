# FROSTMOURNE bootstrap — local-only x86 diagnostic test

This package is an independent local DLL smoke test. It **does not attach to or modify WoW.exe**. It does not validate remote DLL loading or publisher policy.

## Package layout
- `FrostmourneLocalSmoke.exe`: x86 Windows console test host.
- `FrostmourneBootstrap.dll`: x86 DLL with versioned C diagnostic ABI.
- `test_manifest.json`: exact SHA256 and size of each shipped executable.

No game executable, client hooks, injected code or game files are included.

## First local test
1. Extract all package files to an ordinary writable folder outside the game installation.
2. Double-click `FrostmourneLocalSmoke.exe`. The new tester shows a persistent result dialog with success or failure and the numeric error code. Click OK to close it. Do **not** copy the DLL into the game folder.
3. A successful run prints `LOCAL_BOOTSTRAP_SMOKE_PASS; no other process was accessed`.
4. The DLL writes a local UTF-16LE diagnostic log under `%LOCALAPPDATA%\Frostmourne\logs\bootstrap-<PID>.log`. The PID belongs to the smoke tester, **not** Wow.exe.
5. On error, send the text and numeric code shown in the result dialog. On success, send the success message and optionally the matching DLL log. Do not retry with elevated privileges or modify Whitemane files.

`FrostmourneLocalSmoke.exe` starts its own process, loads the DLL using Windows `LoadLibraryW`, calls the ABI exports from the same process, checks the returned result, and unloads the DLL after an explicit shutdown. It neither scans running processes nor opens another process handle.

## Scope and evidence
Passing CI confirms x86 compilation, local x86 execution, PE/dependency checks, package integrity, and successful self-process initialization on the CI Windows runner. It **does not** establish compatibility with an already-running game, game stability, permissions to load a third-party DLL into a game, or an end-to-end in-game success.

This diagnostic experiment is not an active runtime release. It must not be installed via the in-game updater, written to `runtime/current.json`, or promoted to `main` without explicit acceptance.
