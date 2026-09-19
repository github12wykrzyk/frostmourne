# FROSTMOURNE standalone updater (experimental, work only)

The updater is an external Windows x86 GUI program. It is independent of any injected game module and does not alter launcher code, defeat publisher protection or inject into a running client.

## Build / distribution

GitHub Actions `.github/workflows/verify.yml` builds `FrostmourneUpdater.exe` using Windows Python 3.12 x86 and pinned PyInstaller. The `frostmourne-updater-win-x86` Actions artifact is the user-installable ZIP. Do not put it in the current in-game runtime manifest until a verifiable reproducible EXE and distribution source are registered.

## First use

Extract `FrostmourneUpdater.exe` to a writable folder outside the Whitemane launcher-managed game directory, then launch it. Select the game directory containing the exact registered `Wow.exe`, normally `%LOCALAPPDATA%\Whitemane\Games\FrostmourneRebuffed`. The program checks SHA256 `edba72ae4188bda717eec73b733aab9cb2f4ab7d4a1e22b44e60d81743648ebd` before updating or launching. Only the exact registered x86 client is supported.

This repository is **private**. Provide a fine-grained GitHub token with read-only Contents permission on `github12wykrzyk/frostmourne` in the GUI. The token remains only in memory for this session; never put a token in the repository, command line, issue, report or manifest.

The current `updates/work.json` is deliberately empty: there is no approved in-game runtime build yet. The updater may verify the existing client and report no files available. Do not publish a release or add DLL entries until their real binaries, provenance, dependency constraints and SHA256 pass verification.

## Release manifest contract

`updates/work.json` is fetched over HTTPS using the GitHub Contents API. Its `target_client_sha256` MUST equal the audited client fingerprint. A nonempty release requires a unique `release_id` and `files`, each with `component`, installation-relative `path`, repository-relative `source_path`, `version`, `kind` (`exe`, `dll`, `data`), `arch`, `size_bytes`, lowercase `sha256`, and `depends_on` (component IDs). `compatibility_sets` groups components that must update as one set. An unchecked component is permitted to remain only if its installed bytes ALREADY match that file in the release; dependencies are always selected.

The updater refuses an altered `Wow.exe` even if falsely labelled as a new release; modified client builds need their own audited fingerprint and compatibility policy first. Release publishing must derive the updater manifest from actual exact runtime artifacts, not from guessed filenames. The manifest is read from branch `work`, NOT from `main` or user-provided endpoints. While `work` is experimental, selecting update means accepting an experimental build.

## Failure and rollback semantics

Updates stage all downloads under `<game>/.frostmourne/stage/`, verify their exact byte counts and SHA256, validate dependency closure, check that the registered Wow.exe is present, and reject installing while the detected game is running. Existing managed files are backed up in `<game>/.frostmourne/backups/`. A durable journal is written before replacement; if interrupted during activation, next startup rolls back with intact backups. The installed release manifest is published only after verifying every file of the intended release. `Rollback` restores the immediately preceding recorded version; it does not undo Whitemane launcher updates and does not forcibly stop the game. Keep the user-controlled `.frostmourne` directory intact while updates or rollback might be required.

Windows cannot atomically replace an arbitrary group of open EXE/DLL files. The journal and per-file rollback implement recoverability, not a falsely claimed single-step atomic switch. No remote crash reporting exists: "Zapisz raport lokalny" exports a user-selected JSON file containing version/hash/log metadata only, without automatic upload.

## Verification

Run `python -m unittest discover -s tests -v`, `python tools/verify_current.py`, `python tools/verify_repo.py`, and `python tools/verify_reference_client.py`. The Windows CI job builds and checks an x86 PE updater, then uploads the ZIP. A successful CI build does not substitute for an end-to-end test with the official Whitemane launcher and actual game process.
