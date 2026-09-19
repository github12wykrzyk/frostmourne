# Stable baselines and rollback

A stable baseline is a user-accepted state, not every commit.

`CURRENT.json` points to `artifacts/baselines/<id>/manifest.json`. Baseline metadata lists files necessary to reconstruct and validate that stable state.

Rollback:
1. Preserve diagnostics outside managed runtime paths.
2. Select the accepted stable commit/baseline.
3. Run both verifiers.
4. Recreate the runtime ZIP with `tools/make_runtime_package.py`.
5. Replace only files managed by the runtime manifest.

Future updater behavior: download a complete compatible manifest set, verify all digests before activation, then replace managed files transactionally. Independent DLL/EXE updates are allowed only when dependency and compatibility constraints still pass.
