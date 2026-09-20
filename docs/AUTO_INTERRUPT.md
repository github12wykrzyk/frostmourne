# FROSTMOURNE Auto Interrupt — isolated decision engine (work only)

Target: registered WoW 3.3.5a build 12340 x86, SHA256
edba72ae4188bda717eec73b733aab9cb2f4ab7d4a1e22b44e60d81743648ebd.

## Implemented in this iteration
- Pure C decision engine for target -> focus; Rogue-only guard; PvP/PvE toggles;
  minimum reaction, maximum time remaining, safety margin, blacklist, current range,
  cooldown/energy and interruptibility checks.
- Every candidate is observed independently **twice**. GUID, cast token, spell ID,
  world epoch and cast timestamps must match. Unknown/stale state causes no action.
- One attempted action per distinct cast, even when the adapter rejects it.
  History saturation stops further actions instead of forgetting cast identities.
  Cross-instance history resets only when the verified adapter signals a new world epoch.
- Settings are initialized OFF. GUI, persistence, wider enemy enumeration, actual
  character state reading, Kick and confirmation are **NOT implemented**.
- The optional adapter is intentionally absent: no undocumented WoW function is
  invoked, no memory address/hook is used, no DLL is loaded into the game.
  CI compiles/tests an independent x86 executable, not a gameplay DLL.

## Integration gate
A verified, game-thread-safe adapter must produce cast token, GUID, observed clock,
up-to-date interruptibility and real spell range; its action callback must *again*
validate intended GUID/cast at the point where Kick is issued. Two snapshot reads
alone cannot make a native action atomic. Verify Lua API registration/call conventions
and the exact target binary before any client-dependent adapter. A callback returning
ATTEMPTED is not an INTERRUPT_CONFIRMED event; server/combat log confirmation
is a separate requirement. Do not wire the provisional static offsets to production.

## Testing
GitHub Actions auto-interrupt-engine-x86 runs the exact-client and repository
verifiers, builds with MSVC /W4 /WX /O2 in x86 mode, and asserts:
attempt once, duplicate suppression, noninterruptible/instant cast skip,
stale/cancelled cast skip, window/range/resource checks, second-read race,
rejection suppression, fail-closed adapter and disabled state.
No local gameplay test is requested for this non-game, isolated engine.

## Read-only in-game diagnostic package

The experimental GUI-loader package now includes a read-only in-process Lua-binding
fingerprint probe and a separate non-communicating Lua addon for observing actual casts.
See docs/GUI_LOADER_TEST.md. No live-unit DLL adapter or automatic Kick exists.

## ASAP test build and independent updater

The experimental work-channel module defaults to ASAP: the Lua addon observes a
fresh, interruptible target cast every 100 ms and submits a marker immediately,
without waiting for the legacy loader's 800-ms remaining-time window. A rejected
marker can be retried up to four times at least 150 ms apart while the SAME
live cast and target remain valid. The native bridge independently checks the
fresh GUID and cast immediately before an attempt, limits the remaining time to
30 seconds, and retains a 150-ms end-of-cast margin and at-most-once action per
GUID/cast. The old loader's "Pozostaly czas (ms)" control is not used by this
ASAP module; it remains a legacy compatibility field until a separate loader
GUI update. The addon must be explicitly enabled and the bootstrap module
selected. `kick-<PID>.log` is created at native hook initialization and
records either the received marker/rejection reason or the attempt. A hook
or a cast attempt is NOT confirmation of the server interrupt. The actual
`SPELL_INTERRUPT` combat log is required to claim gameplay success.

## 2026-09-20 experimental action-trial status

The independent bootstrap native Kick bridge (src/auto_interrupt/kick_native_bridge.c)
provides an **opt-in** action request for the selected target only. It is not
connected to the independent core engine above, and no gameplay confirmation has
been received. See docs/GUI_LOADER_TEST.md for marker/precheck/request and
SPELL_INTERRUPT evidence separation. In-game test is required; a verified hook
and a DLL cast request do not prove a successful Kick. Do not promote to main.
