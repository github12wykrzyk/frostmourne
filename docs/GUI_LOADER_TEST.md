# FROSTMOURNE — GUI launcher + test DLL (experimental work branch)

This package contains an independent x86 Windows GUI, a prebuilt x86 FrostmourneBootstrap.dll, its build-time SHA256/size pin, and a package manifest. It does NOT include Wow.exe or the previous FrostmourneLocalLoad.exe. It does not modify any game files.

## Scope and strict result interpretation

The loader launches the selected Wow.exe via .NET Process.Start with UseShellExecute=false (Windows CreateProcess), verifies the selected on-disk executable against the registered build 12340 x86 client (file name, size, PE architecture, version resource, SHA256), tracks its PID and exit code, and stays open. The GUI logs diagnostics in %LOCALAPPDATA%\Frostmourne\logs. The pin in bootstrap.sha256 records the exact included DLL compiled in CI; it is an integrity check, NOT a cryptographic signature or publisher attestation.

The bundled DLL is parsed and SHA256-checked but never loaded or initialized inside Wow.exe: **there is no verified supported Whitemane extension mechanism in this project**. No in-process load, proxy, injection, hook, anti-cheat bypass or elevation has been implemented. A successful game launch is NOT a successful DLL initialization test. GUI always shows "TEST DLL W WOW: NIEPRZETESTOWANE" after game launch, even if the DLL passes disk verification. Bootstrap's existing exported ABI remains unchanged and its prior isolated smoke test does not establish behavior inside Wow.exe.

Only the bundled, pinned FrostmourneBootstrap.dll is approved for disk verification. Additional DLLs may be added/disabled in the GUI, but enabling an external DLL without its reviewed ABI/dependency manifest fails validation and blocks launch. The package-time audit checks x86 PE32, non-DLL vs DLL flags, bootstrap exports, DLL imports/dependencies and hashes. Do not treat package-time checks as a security approval for arbitrary modules.

## Native extension gate audit (2026-09-20)

The project reviewed the registered PE32/I386 client (`reference/client/pe_audit.json`), GUI supervisor and bootstrap exports. The PE imports `LoadLibraryA`, `DINPUT8.dll` and other system/vendor DLLs, but this is not evidence of a public plug-in protocol for arbitrary external native DLLs. The static client export table exposes `AssertAndCrash`, not an extension registration/initialization API. The publisher describes its own internal native client extension but does not document a third-party DLL ABI, loader registration interface or permission for the project bootstrap. Ordinary game UI add-ons use the Lua interface and do not load native DLLs. No supported external native extension mechanism was established for this exact client.

The GUI therefore reports **mechanizm_rozszerzen=BRAK** (no VERIFIED mechanism), followed by **zaladowanie_DLL=NIEPRZETESTOWANE**, **inicjalizacja_ABI=NIEPRZETESTOWANE** and **potwierdzenie_PID_wewnatrz_DLL=NIEPRZETESTOWANE**, with `win32=NIE_DOTYCZY` because no native load was attempted. It also logs separately disk verification and game launch PASS/FAIL and actual CreateProcess Win32 errors where available. `BRAK` means unavailable under this task's verified-supported-mechanism requirement, not proof that no private mechanism exists. Existing debug/release bootstrap and GUI remain diagnostic artifacts; the prior Defender detection of FrostmourneLocalLoad.exe is not assumed false-positive. No game or DLL initialization is simulated.

Result: **in-process DLL initialization was NOT carried out**, and the diagnostic game launcher still starts the exact fingerprint-pinned Wow.exe without changing it. Do not mistake the shipped ZIP for an in-process test package. The operator must provide a documented authorized native extension contract before a subsequent in-process implementation can be designed.

## Use on an authorized local client

1. Extract the whole ZIP outside the game folder, preserving FrostmourneGui.exe, FrostmourneBootstrap.dll and bootstrap.sha256 together. Do not run FrostmourneLocalLoad.exe and do not add antivirus exclusions.
2. Double-click FrostmourneGui.exe (Windows 10/11 with .NET Framework 4.x). The included bootstrap DLL is added automatically, enabled, and will be checked on launch. Choose the actual Wow.exe of the **registered exact build 12340 x86 client**; a different launcher, patched executable, or version fails verification. The GUI does not modify the official Whitemane launcher or game executable.
3. Press URUCHOM WOW. If fingerprint and pinned DLL verification both pass, the GUI attempts a standard direct game launch and displays the actual PID or an explicit Win32/launch error. If Whitemane requires its own launcher, authentication arguments or environment, direct launch might fail. Do not bypass these requirements.
4. Observe the game process and report whether it launched and stayed running. The DLL-in-game status remains NIEPRZETESTOWANE because no authorized extension entry point is established.
5. For debugging, click "Otworz katalog logow" or open %LOCALAPPDATA%\Frostmourne\logs. Attach the newest gui-loader-*.log with the process PID, error code, file checks and exit code, omitting private account or unrelated path information if needed. Saved GUI configuration is at %LOCALAPPDATA%\Frostmourne\loader.cfg.

Close the GUI normally; doing so does not terminate the game. No DLL load retries are performed. There is no game injection functionality in the package.

## CI / verification boundary

CI checks reference client fingerprint, active runtime manifest and repo, compiles the pre-existing diagnostic DLL with MSVC x86 and the WinForms GUI for x86, validates exact PE32 architecture/ABI exports/linked imports, writes SHA256 pin, verifies ZIP content and exercises the no-game self-test. CI does NOT launch Wow.exe or claim in-process initialization. The experiment is excluded from active runtime and main until the user accepts it.

## Debug binaries

The ZIP also contains `FrostmourneGuiDebug.exe` and `FrostmourneBootstrapDebug.dll`, both x86, plus PDB symbols. The debug DLL preserves the same ABI and minimal DllMain; its initialization log marks `build=debug`. These artifacts are diagnostic only and remain outside the active runtime. The debug GUI still does not inject or load any DLL into Wow.exe.
