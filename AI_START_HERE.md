# FROSTMOURNE — AI START HERE

**Top priority:** shortest practical route from user request to a **real, compiled, verified and packaged DLL that performs the requested gameplay function** in World of Warcraft 3.3.5a build 12340, Windows x86. Research, architecture and unit tests serve implementation; they are not substitute deliverables. Follow `AGENTS.md` and `docs/DLL_DELIVERY.md`.

## Exact reading order
`AGENTS.md` → `AI_START_HERE.md` → `AI_INDEX.json` → `CURRENT.json` → `runtime/current.json` → only relevant module files. Work from live GitHub, not chat summaries or old packages. `work` = development; `main` = last user-accepted baseline.

## Confirmed reference client
- `reference/client/Wow.exe`; SHA256 `edba72ae4188bda717eec73b733aab9cb2f4ab7d4a1e22b44e60d81743648ebd`; 7,704,216 bytes.
- PE32/I386, file version `3, 3, 5, 12340`.
- Before client-dependent changes run `python tools/verify_reference_client.py --path reference/client/Wow.exe`. Check actual active EXE fingerprint independently before claiming a different on-disk game EXE is compatible.
- Never reuse WoW 1.12 source/offsets/hooks or offsets from another 3.3.5a binary.

## Fast implementation path
1. Find the existing canonical module and loader entry in `AI_INDEX.json`; inspect only needed sources and build steps.
2. Implement the smallest complete game-facing behavior on `work`, with real adapter, GUI/persistent settings where needed and safety checks. Reuse existing DLL/loader instead of producing isolated logic indefinitely.
3. Build Windows x86 DLL immediately; use/extend the relevant GitHub Actions workflow to package all required compatible components and publish a ZIP. Do not deliver an EXE-only decision-engine test as a playable DLL.
4. Run `python tools/verify_current.py`, `python tools/verify_repo.py` when required by repo/baseline changes, and exact-client verification for client-dependent work; verify PE/ABI/hash/package/dependencies. Share the *actual* artifact and exact commit.
5. Separate `CORE_ONLY`, `DLL_BUILT`, `IN_PROCESS_TESTED`, `GAMEPLAY_TESTED` and `STABLE_ACCEPTED`. Only user-accepted tested code goes to `main`; sync `work` afterwards.

## Current status (do not overclaim)
The accepted `infra-0.1.0` baseline is infrastructure-only; `runtime/current.json` lists no active game runtime. `src/auto_interrupt` is a decision engine without a verified game adapter, and `src/auto_pickpocket` is an inert core linked into an experimental bootstrap DLL. The existing GUI-loader CI artifact tests loading/ABI, **not completed gameplay behavior**. For a new task, re-read live manifests and module docs; this section is historical status, not authority over newer GitHub state.

A feature request ends with a tested gameplay DLL ZIP if achievable, or a precise blocker and an honestly labeled partial artifact. Never report a CI PASS as proof of in-game actions.
