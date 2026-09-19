# Verified Whitemane Frostmourne Rebuffed PE inventory

Status: static, byte-verified PE audit from 2026-09-20. No client process was launched, no signature chain validated, and no code/function-hook equivalence established.

## Exact input and reproduction

- Client: World of Warcraft 3.3.5a build 12340, Windows x86.
- Canonical input: reference/client/Wow.exe; registration: reference/client/reference.json.
- SHA256: edba72ae4188bda717eec73b733aab9cb2f4ab7d4a1e22b44e60d81743648ebd.
- Size: 7,704,216 bytes.
- Initial Git blob SHA: 1b389cbe88172e8ed3e64d5af075150c8fb3e419; not the same identifier as file SHA256.
- Full PE report, including all imported symbols, raw section sizes and directory values: reference/client/pe_audit.json.
- Reproducible generator: tools/audit_reference_client.py, pefile pinned to 2024.8.26 in CI. GitHub Actions runs the audit after tools/verify_reference_client.py and uploads a JSON artifact. The auditor checks SHA256 and size and reads, never executes or patches, the target.
- To repeat in a compatible checkout: python tools/verify_reference_client.py ; python tools/audit_reference_client.py --output dist/reference-client-pe-audit.json.
- The report is observed metadata, not a second fingerprint or active runtime entry.

## Main PE/COFF properties

| Field | Observed |
| --- | --- |
| DOS signature / PE header file offset | MZ / 0x110 |
| PE Machine / format | 0x014c I386 / PE32 (optional header magic 0x10b) |
| COFF Characteristics | 0x0127; EXE; LARGE_ADDRESS_AWARE; RELOCS_STRIPPED |
| COFF timestamp (untrusted provenance field) | 1277448958 |
| Optional Header size / section count | 224 bytes / six |
| ImageBase | 0x00400000 |
| EntryPoint RVA / preferred VA | 0x00001000 / 0x00401000 |
| SizeOfImage / SizeOfHeaders | 10,473,472 / 1,024 bytes |
| SectionAlignment / FileAlignment | 4,096 / 512 bytes |
| Subsystem | 2 (Windows GUI) |
| Stack reserve / commit | 1,500,000 / 4,096 bytes |
| Heap reserve | 1,048,576 bytes |
| Stored / computed PE checksum | 0x00762696 / 0x00762078 (different) |
| DllCharacteristics | 0x8100 |

A checksum difference does not establish why the bytes changed, whether the EXE runs, or whether an Authenticode signature is valid. The SHA256 is the authoritative project identity; do not change it just to obtain PASS.

Fixed version resource: file version 3.3.5.12340; product version 3.3.0.0. String resources include FileVersion = 3, 3, 5, 12340; ProductName = World of Warcraft; CompanyName = Blizzard Entertainment; OriginalFilename = WoW.exe; FileDescription = World of Warcraft Retail. These identify the resource and do not prove equivalence to an unmodified Blizzard binary.

## Sections

| Name | RVA | VirtualSize | Raw offset | Raw size | Flags |
| --- | --- | ---: | --- | ---: | --- |
| .text | 0x00001000 | 6,149,043 | 0x00000400 | 6,149,120 | RX |
| .rdata | 0x005df000 | 878,201 | 0x005dd800 | 878,592 | R |
| .data | 0x006b6000 | 3,253,512 | 0x006b4000 | 495,104 | RW |
| .zdata | 0x009d1000 | 4,096 | 0x0072ce00 | 4,096 | RWX |
| .tls | 0x009d2000 | 25 | 0x0072de00 | 512 | RW |
| .rsrc | 0x009d3000 | 170,704 | 0x0072e000 | 171,008 | R |

The .zdata section is both writable and executable by its PE flags. Its purpose and whether it is vendor-specific are **unverified**. These section RVAs are not gameplay function offsets.

## Imports, exports, TLS and protection

- Import directory: RVA 0x006b2d10, 360 bytes; import address table: RVA 0x005df000, 1,948 bytes.
- 17 static DLL imports and 470 imported functions: KERNEL32.dll, OPENGL32.dll, VERSION.dll, IMM32.dll, WININET.dll, WS2_32.dll, DINPUT8.dll, USER32.dll, GDI32.dll, ADVAPI32.dll, SHELL32.dll, DivxDecoder.dll, WINMM.dll, MSACM32.dll, SETUPAPI.dll, HID.DLL, ole32.dll. All imported symbols are in pe_audit.json.
- Selected imported APIs: LoadLibraryA, FreeLibrary, CreateThread, VirtualProtect, FlushInstructionCache, SetUnhandledExceptionFilter, CreateProcessA and DirectInput8Create. Import presence alone does not prove actual invocation; dynamically loaded libraries are not exhaustively enumerated.
- Delay-import directory: absent. Export directory: RVA 0x006b5630, 73 bytes. One export AssertAndCrash (ordinal 1, RVA 0x004c51d0); export presence is not evidence it is safe for callbacks/hooks.
- Base-relocation directory: RVA 0, size 0; RELOCS_STRIPPED is set. DYNAMIC_BASE is unset. Do not assume the original EXE can be relocated freely. A separately built DLL has its own ASLR/relocation rules.
- TLS directory: RVA 0x006aef70, 24 bytes, with a TLS data region and no nonzero TLS callbacks found by the static parser. This does not establish runtime initialization order or TLS callbacks in other DLLs.
- Load-config and bound-import directories: absent. Debug directory: RVA 0x005e0b10, 28 bytes, with a type-2 (CodeView) record, size 107 bytes. Availability of valid PDB/symbols is unverified.
- DllCharacteristics: NX_COMPAT set; DYNAMIC_BASE, NO_SEH, Guard CF flag unset. Actual process-level mitigations need dynamic checks.
- Security directory: file offset 0x00757c00 (not RVA), size 4,760; certificate data present. Certificate chain, validity and signer identity are unverified. Overlay occupies this same file-offset region; do not assume hidden code is present.

## Whitemane-specific modifications: evidentiary status

The provenance ties this exact SHA256 to the observed Whitemane installation. Case-insensitive byte scanning found zero ASCII Whitemane/Frostmourne markers; absence proves nothing about code changes. No known authentic stock client was byte-diffed, no function was disassembled and no process was dynamically checked. Therefore no vendor-specific patch, hook, offset, custom loader or anti-cheat behavior has been confirmed. In particular, .zdata, checksum mismatch or certificate content must not be attributed to Whitemane without independent evidence.

## Constraints for future DLL development

1. Check this SHA256, PE32 I386 and version before each client-specific investigation or compatibility decision. Reject another client instead of guessing equivalence.
2. Keep loader/updater outside the unmodified reference EXE. Distinguish real Wow.exe by verified full image path, image hash and bitness, not merely launcher process names.
3. Make x86, dependencies, thread affinity, loader lock, lifecycle and crash diagnostics explicit contracts. DllMain must not perform substantial initialization.
4. Static import of DINPUT8.dll makes proxy loading theoretically possible, but forwarding, correct dependency resolution, load ordering, launcher file writes, third-party controls and live compatibility remain **unverified**. Do not implement a proxy based only on this PE finding.
5. Before any production hook, independently establish exact function/structure semantics for this build in authorized tests. Do not reuse offsets/code from WoW 1.12.

## Deferred verification

- Controlled launch of the exact executable through Whitemane; verify running image hash/path, loaded DLLs, initialization order, dependency presence and process mitigations.
- Windows Authenticode validation and trusted stock same-build comparison, if signature/provenance or vendor-modification claims are required.
- Launcher/updater overwrite behavior, compatibility with separately loaded modules, shutdown behavior, and crash-report collection.
- No live-client loading, injection or gameplay test has been performed in this audit.
