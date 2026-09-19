# Manifest contract

`runtime/current.json` is the only active runtime manifest.

Each future entry in `files` requires:
- `component`: stable component ID
- `path`: repository-relative runtime path
- `version`: component version
- `sha256`: lowercase SHA256 of exact bytes
- `arch`: `x86` for PE files
- `canonical_source`: existing repository source path
- `depends_on`: component ID array
- `kind`: `exe`, `dll`, or `data`

PE files are parsed directly; machine type must be I386 (0x014c). Missing files, stale hashes, duplicate components/paths, broken dependencies and missing canonical sources fail verification.

`compatibility_sets` can define groups that must remain mutually compatible. Empty sets are valid while no runtime exists.
