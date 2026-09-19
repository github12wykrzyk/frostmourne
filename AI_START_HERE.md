# AI START HERE

FROSTMOURNE targets **World of Warcraft 3.3.5a build 12340 on Windows x86**.

Current state: infrastructure-only. There is no game module or active runtime binary yet.

## Fast path
1. Read `CURRENT.json`.
2. Read `runtime/current.json`.
3. Read `AI_INDEX.json`.
4. Work on branch `work`.
5. Run:
   ```bash
   python tools/verify_repo.py
   python tools/verify_current.py
   ```
6. Build x86 and package the exact test artifact.
7. Only a user-accepted, verified build is promoted to `main`.

Do not copy code or binaries from the separate wow112 project. Do not invent modules in manifests before they exist.
