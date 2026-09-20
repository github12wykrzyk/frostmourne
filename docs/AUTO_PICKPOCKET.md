# FROSTMOURNE — Auto Pickpocket (experimental in-process core; NOT game-ready)

Target: Whitemane Frostmourne Rebuffed WoW 3.3.5a build 12340 Windows x86.
Status: **client-independent decision engine embedded in the experimental bootstrap DLL**. GitHub Actions provides a complete GUI loader ZIP with a new bootstrap status export. A successful local test can confirm that the decision core initializes within Wow.exe; it does NOT confirm any Pick Pocket gameplay action. No WoW-native object enumerator, in-game GUI, cast bridge or outcome event binding exists. The original ABI 1.0 remains intact; the GUI loader now checks the additional read-only AP core status. Never claim that Pick Pocket fires in WoW based on loader/ABI/CI PASS.

Sources: src/auto_pickpocket/auto_pickpocket.{h,c}, integrated by src/bootstrap/bootstrap.c and read out by src/loader/RemoteBootstrap.cs. No code/offsets imported from other client versions. Tests: tests/test_auto_pickpocket.c and src/bootstrap/local_smoke.c. CI: .github/workflows/auto-pickpocket-core.yml, gui-loader-x86.yml and bootstrap-local-smoke.yml.

## Core semantics

- Default disabled; Rogue/alive/Stealth/non-combat/non-busy, spell-available and verified-adapter gates. Target/focus/mouseover/nearby are input source flags; the core cannot itself enumerate NPCs or select targets.
- Each adapter-supplied snapshot must identify a live, hostile, pickpocketable NPC by its real GUID and verified eligibility, true spell range, line of sight and native ability preflight. Unknown = skip; neither scan radius nor a magic distance establishes actual Pick Pocket range.
- At most one in-flight attempt. The adapter must return ISSUED only when it actually issued one action in the safe game thread. If it declines, the core does not record a cast.
- Success/failure/unknown must come from correlated verified client/server events using fm_ap_report. Casting alone is never success. A timeout/interruption becomes UNKNOWN; it is not automatically retried. A confirmed FAILED result has a configurable retry interval; SUCCESS never retries during the same world epoch. History capacity is 256 GUIDs, after which new GUIDs fail closed until world reset.
- The engine stores GUIDs, never unit pointers. The host MUST change world_epoch on session, map/instance or object-domain transitions; event correlation, expiry and validation belong to the future bridge. Blacklist of GUID and NPC entry is supported; changes to config and blacklist are not persisted by this core.
- No automatic target change, movement, stealth manipulation, casting hook or game-thread creation. Opener hold only reflects a pending attempt and has an independent bounded deadline; this module does not actually coordinate any opener yet.

## Integration gates before a playable ZIP

1. Verify the EXACT reference/client/Wow.exe fingerprint from reference/client/reference.json. Audit functions/layouts against that binary before relying on any native address.
2. Establish a reliable read-only object enumerator, unit eligibility/type/LOS/real spell-range checks, GUID and world-epoch management, safe action-thread dispatch and correlated cast outcome events. Handle zone/relog, target changes, NPC despawn and races at each action. No offset or hook may be guessed from WoW 1.12 or a different 3.3.5a EXE.
3. The client-independent core now compiles into a new package-pinned bootstrap DLL, and the existing loader checks its inert status. A future verified adapter must use an explicit versioned contract and validated compatibility/dependency manifest; do not bypass the existing loader's exact SHA256 allowlist. Only after verifying the adapter add gameplay GUI toggles, atomic config persistence, detailed gameplay diagnostics and full Windows x86 packaging.
4. Run verify_reference_client, verify_current and verify_repo and CI; run authorized local gameplay tests separately. A native compilation test is not proof of actual in-game behavior.

This is an experimental test ZIP, not a user-accepted stable release. No change to CURRENT.json, runtime/current.json or main is warranted. The GUI package includes a NEW build-time SHA256 pin for its bundled DLL; do not mix versions. See docs/GUI_LOADER_TEST.md for the limited in-process test.
