# FROSTMOURNE — experimental x86 GUI loader and in-process bootstrap test

Target: the exact registered WoW 3.3.5a build 12340 x86 client, SHA256 `edba72ae4188bda717eec73b733aab9cb2f4ab7d4a1e22b44e60d81743648ebd`.

## Runtime behavior

The existing WinForms GUI accepts an executable with any `.exe` filename, including a renamed Wow.exe, but requires the **same exact registered client bytes** (x86 PE, 7,704,216-byte size, build 12340 version resource and pinned SHA256). Renaming a different launcher or executable does not bypass client verification. The GUI verifies the bundled FrostmourneBootstrap.dll (x86 PE, size, exact SHA256), starts the selected client through Process.Start and identifies its PID. With the package bootstrap enabled, the GUI performs **one explicit standard Windows remote LoadLibraryW attempt** on the exact process that it launched, then resolves and calls Frostmourne_GetAbi and Frostmourne_Initialize within that process. It checks the module's loaded path and base address, ABI major/minor, the initialization packet, Win32 result and DLL-observed PID. Each stage is logged independently; a successful game launch is NOT evidence that the DLL loaded or initialized.

The in-process loader is in src/loader/RemoteBootstrap.cs; src/bootstrap/bootstrap.c and bootstrap.h retain the pre-existing ABI 1.0 and minimal DllMain. No gameplay hooks, DLL proxy/hijacking, stealth, elevation fallback, client EXE modification, security-policy changes, retry after failure or anticheat bypass are implemented. This is a Windows OS loading experiment, **not** a publisher-supported Whitemane plugin API. The launcher/publisher may disallow this behavior or the game/security system may prevent the load. A failure is reported, not bypassed. The previous FrostmourneLocalLoad.exe, which had been flagged by Defender, is neither reused nor packaged; no safety or false-positive claim is made about antivirus results.

The package includes one approved bootstrap DLL with an exact build-time SHA256 pin. The GUI retains adding/removing/checking DLLs, but unreviewed external DLLs are NOT loaded: an enabled DLL without a compatibility/ABI/dependency manifest blocks the attempt. A multi-module manager and a supported third-party extension interface have not been implemented. Debug DLL is provided for later diagnostics, not automatically selected. No active game runtime or stable baseline is changed.

## Windows local test

1. Extract the full ZIP **outside** the game directory, keeping FrostmourneGui.exe, FrostmourneBootstrap.dll and bootstrap.sha256 together. The ZIP contains no Wow.exe.
2. Launch FrostmourneGui.exe on an authorized Windows x86-capable machine with .NET Framework 4.x. Choose the EXACT registered target client executable under any `.exe` filename, keep the bundled FrostmourneBootstrap.dll checked, and press URUCHOM WOW once. The loader does not retry failed loads.
3. Verify that the game launches. Read the GUI result and the current gui-loader-*.log in `%LOCALAPPDATA%\Frostmourne\logs`. A full PASS requires the GUI to log DLL load PASS, GetAbi PASS, Initialize packet magic PASS and matching DLL-observed PID; the native bootstrap also writes `bootstrap-<PID>.log` in the same logs directory. A game PID or an exit code of 0 alone is insufficient.
4. If the attempt fails, preserve the GUI log and the exact Win32 stage/error without disabling Windows Defender, replacing official DLLs or modifying Wow.exe. If a native thread times out, its state is unknown; do not retry automatically. The game may keep running even if bootstrap initialization fails.

The loader's GUI can remain open while the game runs. Closing the GUI does not forcibly terminate Wow.exe or unload an active bootstrap. Disable the bundled DLL checkbox to run game-launch-only diagnostics.

## CI validation boundary

GitHub Actions compiles test/debug bootstrap and release/debug GUI for PE32 x86; runs the exact-client fingerprint, current runtime and repository verifiers; checks exports, imports, architecture, pin and ZIP manifest; and runs no-game GUI self-tests. GitHub Actions does **not** launch Wow.exe or prove that remote loading works with this Whitemane process. Only the user's local runtime log with the same PID and initialized packet can establish that. The experimental ZIP stays on work until the user accepts a proven result.
