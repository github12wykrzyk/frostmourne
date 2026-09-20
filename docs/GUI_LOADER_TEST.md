# FROSTMOURNE — experimental x86 GUI loader and in-process bootstrap test

Target: the exact registered WoW 3.3.5a build 12340 x86 client, SHA256 `edba72ae4188bda717eec73b733aab9cb2f4ab7d4a1e22b44e60d81743648ebd`.


## Auto Interrupt — first live diagnostic (NO automatic Kick)

This package contains the pinned experimental bootstrap with read-only
`Frostmourne_ProbeInterruptBindings` and a separate read-only addon
`FrostmourneCastProbe`. The DLL checks four Lua registration table pairs and
four function prologues **inside the actual launched game process** against the
exact reference binary. It does NOT resolve a live unit pointer, call native
game functions, watch current casts, execute Kick or enable any gameplay
automation. The loader prints `interrupt_bindings=PASS` only after confirming
the same PID, image base, mask=0xF for registrations and code prologues, and
successful in-process initialization. A failed probe stops the diagnostic stage;
the launcher does not attempt to bypass security controls.

The separate addon uses only the ordinary in-game Lua API to observe
`UnitCastingInfo`, `UnitChannelInfo`, and `UnitGUID` for target/focus.
The addon does NOT communicate with the DLL: its samples are independent
evidence, not proof of a native DLL cast read. **The new GUI automatically
installs both packaged addon files** beside the verified game EXE at
`Interface\\AddOns\\FrostmourneCastProbe\\` **before launching the game**,
verifies SHA256 of the installed copies, and logs `ETAP addon_install=PASS`
or the exact failure. Older installations of this specific addon are backed
up before replacement; files not identifiable as our addon are never replaced.
The loader DLL on its own does NOT register the slash command.

After logging in, the chat must print `FM Cast Probe Addon LOADED`. This
confirms that WoW has loaded the Lua addon; the loader's
`CAST BINDINGS=PASS` confirms a separate read-only DLL binding check only.
Type `/fmcast on` (alternative `/fmprobe on`) and select an enemy casting
a spell. Observe GUID, spell name, remaining time, interruptible flag and
cast/channel status. `/fmcast off` stops updates. No Kick is attempted.
If the chat does not print `Addon LOADED`, inspect WoW's AddOns list at
character selection; enable FrostmourneCastProbe. If a game instance was
already open before installation, fully restart the game through the new loader.
The installer operates only on the directory of the EXE selected and verified
in the GUI, not on a separate launcher/alternate game folder.

Use only an isolated, authorized local test. Preserve the GUI log and in-game
cast print or screenshot after one test. Do not disable antivirus or anti-cheat,
combine older loader/DLL/pin files, or treat an addon-only success as native
bridge readiness. The ZIP contains no Wow.exe and does not alter the game client.

## Auto Pickpocket in-process diagnostic (NOT gameplay)

This experimental ZIP now contains an x86 FrostmourneBootstrap.dll linked with
the client-independent Auto Pickpocket decision core. After the existing ABI and
PID checks, the loader invokes `Frostmourne_GetAutoPickpocketStatus` inside the
just-launched WoW process. Expected result: `AP CORE=READY`,
`adapter=NIEZAIMPLEMENTOWANY`, `gameplay_actions=WYLACZONE`. The bootstrap log
also records `auto_pickpocket_core=READY adapter=ABSENT gameplay_actions=DISABLED`.
These strings **do not** mean nearby NPCs have been discovered, Pick Pocket was
cast, or game-client functions were validated. The DLL contains no native game
adapter, no automated targeting, no game memory reads and no gameplay actions.
Do not try to use it as a working Auto Pickpocket feature or modify game settings
to make it cast. A successful result tests only in-process initialization of the
core and the matching ABI/status export.

For this experiment, extract the complete new ZIP outside the game directory
and use its FrostmourneGui.exe, FrostmourneBootstrap.dll and bootstrap.sha256
together. Do not combine the new DLL with a previously downloaded loader or old
SHA256 pin. The existing GUI loader intentionally rejects external/unpinned DLLs.
One game launch with a valid PID, the `auto_pickpocket_core=PASS` GUI log
and the matching `bootstrap-<PID>.log` is sufficient; **no NPC test is expected
to succeed**. CI cannot perform the local in-game test.

## Runtime behavior

The existing WinForms GUI accepts an executable with any `.exe` filename, including a renamed Wow.exe, but requires the **same exact registered client bytes** (x86 PE, 7,704,216-byte size, build 12340 version resource and pinned SHA256). Renaming a different launcher or executable does not bypass client verification. The GUI verifies the bundled FrostmourneBootstrap.dll (x86 PE, size, exact SHA256), starts the selected client through Process.Start and identifies its PID. With the package bootstrap enabled, the GUI performs **one explicit standard Windows remote LoadLibraryW attempt** on the exact process that it launched, then resolves and calls Frostmourne_GetAbi and Frostmourne_Initialize within that process. It checks the module's loaded path and base address, ABI major/minor, the initialization packet, Win32 result and DLL-observed PID. Each stage is logged independently; a successful game launch is NOT evidence that the DLL loaded or initialized.

The in-process loader is in src/loader/RemoteBootstrap.cs; src/bootstrap/bootstrap.c and bootstrap.h retain the pre-existing ABI 1.0 and minimal DllMain. No gameplay hooks, DLL proxy/hijacking, stealth, elevation fallback, client EXE modification, security-policy changes, retry after failure or anticheat bypass are implemented. This is a Windows OS loading experiment, **not** a publisher-supported Whitemane plugin API. The launcher/publisher may disallow this behavior or the game/security system may prevent the load. A failure is reported, not bypassed. The previous FrostmourneLocalLoad.exe, which had been flagged by Defender, is neither reused nor packaged; no safety or false-positive claim is made about antivirus results.

Saved configurations from older loader packages may contain extra `FrostmourneBootstrap.dll` paths (for example, a copy under the game `WTF` folder). On GUI startup, such remembered same-name paths outside the loader package are discarded with a `MIGRACJA` log, and the cleaned configuration is saved; the bundled bootstrap is selected instead. Explicitly adding an external or different DLL later still requires the reviewed compatibility/ABI/dependency manifest and will not be loaded without it. This migration never approves unknown DLL bytes or alters the game installation.\n\nThe package includes one approved bootstrap DLL with an exact build-time SHA256 pin. The GUI retains adding/removing/checking DLLs, but unreviewed external DLLs are NOT loaded: an enabled DLL without a compatibility/ABI/dependency manifest blocks the attempt. A multi-module manager and a supported third-party extension interface have not been implemented. Debug DLL is provided for later diagnostics, not automatically selected. No active game runtime or stable baseline is changed.

## Windows local test

1. Extract the full ZIP **outside** the game directory, keeping FrostmourneGui.exe, FrostmourneBootstrap.dll and bootstrap.sha256 together. The ZIP contains no Wow.exe.
2. Launch FrostmourneGui.exe on an authorized Windows x86-capable machine with .NET Framework 4.x. Choose the EXACT registered target client executable under any `.exe` filename, keep the bundled FrostmourneBootstrap.dll checked, and press URUCHOM WOW once. The loader does not retry failed loads.
3. Verify that the game launches. Read the GUI result and the current gui-loader-*.log in `%LOCALAPPDATA%\Frostmourne\logs`. A full PASS requires the GUI to log DLL load PASS, GetAbi PASS, Initialize packet magic PASS and matching DLL-observed PID; the native bootstrap also writes `bootstrap-<PID>.log` in the same logs directory. A game PID or an exit code of 0 alone is insufficient.
4. If the attempt fails, preserve the GUI log and the exact Win32 stage/error without disabling Windows Defender, replacing official DLLs or modifying Wow.exe. If a native thread times out, its state is unknown; do not retry automatically. The game may keep running even if bootstrap initialization fails.

The loader's GUI can remain open while the game runs. Closing the GUI does not forcibly terminate Wow.exe or unload an active bootstrap. Disable the bundled DLL checkbox to run game-launch-only diagnostics.

## CI validation boundary

GitHub Actions compiles test/debug bootstrap and release/debug GUI for PE32 x86; runs the exact-client fingerprint, current runtime and repository verifiers; checks exports, imports, architecture, pin and ZIP manifest; and runs no-game GUI self-tests. GitHub Actions does **not** launch Wow.exe or prove that remote loading works with this Whitemane process. Only the user's local runtime log with the same PID and initialized packet can establish that. The experimental ZIP stays on work until the user accepts a proven result.
