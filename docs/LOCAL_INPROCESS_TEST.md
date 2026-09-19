# FROSTMOURNE — one-shot local Wow.exe in-process diagnostic (EXPERIMENTAL)

Target: an **isolated local** World of Warcraft 3.3.5a build 12340 x86 process with the exact reference-client fingerprint. This is not a public-server deployment, not an updater release and not an anti-cheat bypass. Do not run this package in a protected online session or where the client operator has not permitted the experiment. Do not disable or work around security controls. The executable never patches Wow.exe on disk, hooks game APIs or adds gameplay behavior.

## What changed from the completed earlier tests

The earlier \`FrostmourneLocalSmoke.exe\` initialized the DLL only inside the smoke tester. The read-only \`FrostmourneProcessCheck.exe\` identified the actual game process and the matching on-disk client but did not load DLLs. **Neither result is evidence of successful in-game initialization.** The new executable \`FrostmourneLocalLoad.exe\` invokes a single standard Windows x86 remote LoadLibraryW operation and, if it succeeds, a single call of the existing exported diagnostic \`Frostmourne_Initialize\` in the target process. DLL loading necessarily allocates and writes private memory in the running process; "no disk EXE modifications" does **not** mean read-only process access.

The existing process checker is reused for unique-PID detection, full image path, x86/WOW64 comparison, exact on-disk client size/SHA256 and initial module inventory. After opening the handle with the rights required for remote loading, the loader re-verifies the process image identity and integrity. The package pins the exact bootstrap DLL SHA256 and size at build time. It refuses a second load if a same-named DLL is already present; unknown module enumeration status or fingerprint mismatch also stops before loading.

The tool then verifies that the returned remote module base equals the independently enumerated module base and that the resolved DLL path matches the tested DLL. Finally it calls the x86 diagnostic ABI, verifies the thread return value, ABI packet status and matching target PID, and checks the game process is still alive. The bootstrap writes its own diagnostic log in the target process. Thread timeouts never result in automatic retries or in freeing memory a still-running remote thread may be using.

A completed result dialog stays visible until the user closes it. Code 0 with a field log showing "ETAP 3: PASS" means **initial in-process initialization was observed at that moment**; it does not prove sustained game stability or permission to use native DLLs in public sessions. CI on a clean Windows runner checks compilation, expected no-game behavior, PE32 architecture, exported ABI, imports, SHA256 and archive integrity; CI **does not run Whitemane**.

## Test on the local computer

1. Use only a controlled, isolated local session in which such an experiment is permitted; ensure exactly one \`Wow.exe\` is running. Do not change the original Wow.exe or Whitemane launcher.
2. Extract the whole \`frostmourne-local-inprocess-x86.zip\` to a normal writable folder **outside the game folder**. Keep \`FrostmourneLocalLoad.exe\` and \`FrostmourneBootstrap.dll\` together. Do not add them to the active updater/runtime.
3. Start the isolated local game, then double-click \`FrostmourneLocalLoad.exe\`. Read the test confirmation and choose Yes only for the intended local authorized experiment. Do not run it as administrator, force permissions, disable security mechanisms or attempt a second load after a failure.
4. Read the result window and close it consciously. If the test reached initialization, the DLL remains loaded without hooks until the game exits normally; do not manually delete or replace its backing DLL while the game runs.
5. Send the complete log shown in the dialog (\`%LOCALAPPDATA%\Frostmourne\logs\local-load-<PID>-<tick>.log\`) and the library log (\`%LOCALAPPDATA%\Frostmourne\logs\bootstrap-<PID>.log\`); include whether the game continued functioning and whether it exited/crashed. Redact unrelated local path or account information before sharing if desired.

If process authorization, module loading or ABI initialization fails, report the precise diagnostic phase and Windows error code. An exit from the game during the attempt is a failed test even if an earlier operation succeeded. No code path includes anti-cheat evasion, stealth loading, privilege escalation, retry loops or modification of the original game/launcher binaries.

## Development and branch state

This is an experimental work-branch-only diagnostic. It is deliberately excluded from \`runtime/current.json\`, \`CURRENT.json\`, \`updates/work.json\` and the accepted \`main\` baseline. The workflow \`.github/workflows/local-inprocess-x86.yml\` creates the independent Windows x86 ZIP after checking \`verify_reference_client.py\`, \`verify_current.py\` and \`verify_repo.py\`. A CI build passing means the *test binary* is ready for a permitted local field test, not that real-client loading or stability has passed.
