# AI START HERE

FROSTMOURNE targets **World of Warcraft 3.3.5a build 12340 on Windows x86**.

Current state: infrastructure plus an audited target-client fingerprint. There is no game module or active runtime binary yet.

Registered target client:
- expected repository path: `reference/client/Wow.exe`
- SHA256: `edba72ae4188bda717eec73b733aab9cb2f4ab7d4a1e22b44e60d81743648ebd`
- size: 7,704,216 bytes
- PE: I386 / x86
- file version resource: `3, 3, 5, 12340`

## Fast path
1. Read `CURRENT.json`.
2. Read `runtime/current.json`.
3. Read `AI_INDEX.json`.
4. Work on branch `work`.
5. Before client-dependent work, run `python tools/verify_reference_client.py --path <Wow.exe>`.
6. Run:
   ```bash
   python tools/verify_repo.py
   python tools/verify_current.py
   ```
7. Build x86 and package the exact test artifact.
8. Only a user-accepted, verified build is promoted to `main`.

Do not copy code or binaries from the separate wow112 project. Do not invent modules in manifests before they exist.
