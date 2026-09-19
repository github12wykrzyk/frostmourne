# FROSTMOURNE — read-only test of the actual Wow.exe process

This is **not an injector** and does not call the bootstrap ABI in Wow.exe. The previous local bootstrap smoke test has passed independently; this program does not repeat it.

## Scope and limits

The standalone PE32/x86 GUI application enumerates running processes and accepts **exactly one** process named Wow.exe. It opens the candidate only for query and synchronization, checks for process exit, obtains its full image path, compares WOW64 state against the x86 diagnostic program, reads the **on-disk** image size and SHA256 (reference: 7,704,216 bytes and `edba72ae4188bda717eec73b733aab9cb2f4ab7d4a1e22b44e60d81743648ebd`), and enumerates its modules. It reports whether a module named FrostmourneBootstrap.dll is visible. It does not call any DLL export, load a DLL into the game, change memory, alter the client executable or launcher, escalate privileges, bypass client protections, or retry failed operations. An observed module is **not proof of ABI initialization**.

A same-digest file on disk is a fingerprint of that file only, not an attestation of unchanged in-memory code. A missing module or access-denied module enumeration does not prove an anti-cheat block or the cause of a failure. Windows permission to open a process does not provide a publisher-approved plugin interface. No verified authorized Whitemane extension API for this purpose has been identified in the repository. Therefore this package does not attempt remote loading. It is a prerequisite diagnostic, not the requested end-to-end in-process test.

## Windows x86 run

1. Launch Whitemane and enter the game normally.
2. Extract `frostmourne-process-check-x86.zip` outside the game installation. It contains `FrostmourneProcessCheck.exe`, this README and `test_manifest.json`. Do **not** copy files into the game directory.
3. Double-click `FrostmourneProcessCheck.exe`. It displays the result in a persistent Windows message box. Close it yourself with OK. You do not need administrator privileges; do not elevate on access-denied results.
4. Record the PID, path, fingerprint status, module status, diagnostic code, Win32 error code (if present), and log path. The UTF-16LE log is saved under `%LOCALAPPDATA%\Frostmourne\logs\process-check-<PID>-<tick>.log`. If no game was found, the PID is 0.
5. Send the complete log and the GUI message for follow-up. If the game exits, report whether it closed before or during the check.

Error codes: 10–12 enumeration errors; 20 no Wow.exe; 21 multiple Wow.exe processes (no automatic choice); 22 process access failure; 23/29 game exited during diagnosis; 24 process image path failure; 25 architecture-query failure; 26 incompatible architecture; 27 on-disk file hash/size read failure; 28 fingerprint mismatch; 30 log-writing failure. Each Windows API failure is accompanied by a numeric Win32 error in the report where available.

Code 0 means **only** the unique Wow.exe PID, readable on-disk reference fingerprint and process lifetime check succeeded. It does not confirm that bootstrap loaded or ran inside the game. Presence of the DLL in a module snapshot and initialization of its ABI are separate questions. The result dialog stays open until you click OK.

## CI evidence

The Windows x86 job compiles the read-only diagnostic using MSVC, executes its no-game branch on a clean runner (CI environment suppresses the dialog), checks PE32 architecture/imports and standalone dependencies, and validates ZIP entries and SHA256. It runs `tools/verify_reference_client.py`, `tools/verify_current.py` and `tools/verify_repo.py`. The job does not run Whitemane and does not claim an in-game outcome.

No client EXE or bootstrap DLL is distributed in this diagnostic package. `runtime/current.json`, `CURRENT.json`, `main` and the existing bootstrap ABI remain unchanged.
