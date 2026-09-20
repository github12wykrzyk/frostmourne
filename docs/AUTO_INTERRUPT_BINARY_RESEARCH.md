# FROSTMOURNE: verified static cast / spell call-path research (2026-09-20)

Target: the **exact** registered Whitemane WoW 3.3.5a build 12340 Windows x86 executable,
`reference/client/Wow.exe`, SHA256 `edba72ae4188bda717eec73b733aab9cb2f4ab7d4a1e22b44e60d81743648ebd`.
Analysis uses `tools/audit_interrupt_bridge.py` and the read-only CI workflow
`.github/workflows/audit-interrupt-bridge.yml`. GitHub Actions run:
https://github.com/github12wykrzyk/frostmourne/actions/runs/35478822571

## Lua registration entries observed in the executable

Each line records a *name pointer* in .data immediately followed by a code pointer into .text.
That structure, adjacent entries and independently decoded prologues support the claimed Lua
registration mapping. These are image virtual addresses for this exact binary, not generic
WoW addresses, not a supported external plugin ABI, and not safe arbitrary-thread entrypoints.

| Lua name | Registration name pointer in .data | Function address in .text |
| --- | --- | --- |
| UnitCastingInfo | 0x00AD2560 | 0x00611DF0 |
| UnitChannelInfo | 0x00AD2568 | 0x00612090 |
| UnitGUID | 0x00AD22D0 | 0x0060E630 |
| UnitExists | 0x00AD21D8 | 0x0060C2A0 |
| UnitCanAttack | 0x00AD22A0 | 0x0060D730 |
| CastSpellByID | 0x00ACCDF0 | 0x0053E060 |
| CastSpellByName | 0x00ACCDE8 | 0x00540310 |
| GetSpellCooldown | 0x00ACCD18 | 0x00540E80 |
| IsUsableSpell | 0x00ACCD78 | 0x00541680 |

Example disassembly: 0x00611E55 `mov eax,[ebx+0xA6C]` then 0x00611E68
`call 0x004CFD20` (spell-related lookup). 0x00611E75 subtracts
`[ebx+0xA7C]` from a value produced by 0x0086AE20 and branches away on
nonnegative result. The function later reads 0xA78 and 0xA7C while marshalling
Lua return values. `UnitChannelInfo` analogously reads 0xA80, 0xA84 and 0xA88.
This supports the *candidate field semantics*, but not an independent proof that every
pointer reachable at runtime is valid or that the values are synchronized with server time.

| Unit-relative field | Interpretation supported by analyzed Lua function |
| --- | --- |
| +0xA6C | active cast spell ID |
| +0xA78 | cast start time |
| +0xA7C | cast end time |
| +0xA80 | channel spell ID |
| +0xA84 | channel start time |
| +0xA88 | channel end time |

The `UnitCastingInfo` wrapper receives a Lua state-like argument in [ebp+8];
it resolves the unit token through 0x0084E0E0 and 0x0060C1F0. The latter
calls 0x0060ABF0 and 0x004D4DB0 to look up an object. These are
**internal observations**, not a validated external-object-manager contract.

The registered `CastSpellByID` wrapper at 0x0053E060 contains a direct
call at 0x0053E177 to 0x0080DA40. The function beginning at 0x0080DA40
calls 0x0080CCE0. Its wrapper passes multiple stack arguments and cleans up
after the call, but their full semantic types, destination GUID behavior, return
contract, and required game-thread context remain unverified. 0x0080DB50 is
*inside* a separate function starting 0x0080DA80, not a valid standalone
entrypoint for a new DLL call.

## Integration status / prohibited assumptions

- **Static extraction completed**: exact-image fingerprint, registration pairings,
  field reads and cast call-path observations.
- **Not completed**: safe game-thread scheduling, Lua invocation ABI, live GUID-to-unit
  resolution and object lifetime, independent interruptibility semantics, player
  energy/cooldown/range from the real client, GUID-targeted cast dispatch and a
  server-confirmed successful interrupt.
- The `src/auto_interrupt` decision engine remains OFF by default and disconnected
  from all WoW-native calls. A CI PASS does NOT prove in-game Auto Kick.
- Do not call 0x0080DA40 from an arbitrary remote thread, dereference a guessed unit
  pointer, or classify a requested Kick as an acknowledged interrupt.
- `main`, `CURRENT.json`, `runtime/current.json` and active updater payloads
  remain unchanged by this **research-only** iteration.
