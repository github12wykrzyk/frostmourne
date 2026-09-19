# Registered target client

This directory registers the exact Whitemane FrostmourneRebuffed client executable audited for FROSTMOURNE.

Expected binary path: `reference/client/Wow.exe`

Fingerprint:
- SHA256: `edba72ae4188bda717eec73b733aab9cb2f4ab7d4a1e22b44e60d81743648ebd`
- size: 7,704,216 bytes
- PE machine: I386 / x86 (`0x014c`)
- file version resource: `3, 3, 5, 12340`
- product marker: `World of Warcraft`

Use `python tools/verify_reference_client.py --path <path-to-Wow.exe>` before deriving offsets, hooks, structures, or binary patches from a client executable.

The metadata is authoritative for compatibility. The executable is not part of the active runtime manifest until explicitly promoted into runtime packaging.
