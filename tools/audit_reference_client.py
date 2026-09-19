"""Read-only PE audit of the exact registered Frostmourne client.

Requires pefile (pinned by CI). Does not load, execute, or modify the target.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import json
from pathlib import Path

import pefile

ROOT = Path(__file__).resolve().parents[1]
METADATA = ROOT / "reference/client/reference.json"


def text(value):
    if isinstance(value, bytes):
        return value.decode("utf-8", "replace")
    return str(value) if value is not None else None


def hx(value):
    return f"0x{value:08x}"


def directory(pe, index):
    item = pe.OPTIONAL_HEADER.DATA_DIRECTORY[index]
    return {"rva": hx(item.VirtualAddress), "size": item.Size}


def version_info(pe):
    fixed = []
    for obj in getattr(pe, "VS_FIXEDFILEINFO", []) or []:
        fixed.append({
            "signature": hx(obj.Signature),
            "file_version": ".".join(str(x) for x in (
                obj.FileVersionMS >> 16, obj.FileVersionMS & 0xffff,
                obj.FileVersionLS >> 16, obj.FileVersionLS & 0xffff)),
            "product_version": ".".join(str(x) for x in (
                obj.ProductVersionMS >> 16, obj.ProductVersionMS & 0xffff,
                obj.ProductVersionLS >> 16, obj.ProductVersionLS & 0xffff)),
            "file_flags": hx(obj.FileFlags),
            "file_type": hx(obj.FileType),
        })
    strings = {}
    for group in getattr(pe, "FileInfo", []) or []:
        for obj in group:
            for table in getattr(obj, "StringTable", []) or []:
                for key, val in table.entries.items():
                    k = text(key)
                    if k in ("FileVersion", "ProductVersion", "ProductName",
                             "CompanyName", "OriginalFilename", "FileDescription"):
                        strings[k] = text(val)
    return {"fixed": fixed, "strings": strings}


def imports(pe, attr):
    result = []
    for dll in getattr(pe, attr, []) or []:
        entries = []
        for item in dll.imports:
            entries.append({"name": text(item.name), "ordinal": item.ordinal if item.name is None else None})
        result.append({"dll": text(dll.dll), "functions": entries})
    return result


def audit(path, expected):
    payload = path.read_bytes()
    digest = hashlib.sha256(payload).hexdigest()
    if len(payload) != expected["size_bytes"] or digest != expected["sha256"]:
        raise ValueError("Reference client byte fingerprint mismatch; refusing PE audit")
    pe = pefile.PE(data=payload, fast_load=False)
    if pe.FILE_HEADER.Machine != 0x014c or pe.OPTIONAL_HEADER.Magic != 0x10b:
        raise ValueError("Reference client is not a Windows PE32 x86 image")
    d = pefile.DIRECTORY_ENTRY
    fh, oh = pe.FILE_HEADER, pe.OPTIONAL_HEADER
    sections = []
    for sec in pe.sections:
        flags = sec.Characteristics
        sections.append({
            "name": text(sec.Name.rstrip(b"\0")),
            "rva": hx(sec.VirtualAddress),
            "virtual_size": sec.Misc_VirtualSize,
            "raw_offset": hx(sec.PointerToRawData),
            "raw_size": sec.SizeOfRawData,
            "characteristics": hx(flags),
            "read": bool(flags & 0x40000000),
            "write": bool(flags & 0x80000000),
            "execute": bool(flags & 0x20000000),
        })
    relocation_entries = getattr(pe, "DIRECTORY_ENTRY_BASERELOC", []) or []
    reloc_types = collections.Counter(
        str(item.type) for block in relocation_entries for item in block.entries
    )
    tls = None
    if hasattr(pe, "DIRECTORY_ENTRY_TLS"):
        t = pe.DIRECTORY_ENTRY_TLS.struct
        callbacks = []
        if t.AddressOfCallBacks:
            callback_rva = t.AddressOfCallBacks - oh.ImageBase
            for i in range(128):
                address = pe.get_dword_at_rva(callback_rva + i * 4)
                if address is None or address == 0:
                    break
                callbacks.append(hx(address - oh.ImageBase))
        tls = {
            "directory": directory(pe, d["IMAGE_DIRECTORY_ENTRY_TLS"]),
            "raw_start_va": hx(t.StartAddressOfRawData),
            "raw_end_va": hx(t.EndAddressOfRawData),
            "callback_rvas": callbacks,
        }
    exports = []
    if hasattr(pe, "DIRECTORY_ENTRY_EXPORT"):
        for sym in pe.DIRECTORY_ENTRY_EXPORT.symbols:
            exports.append({"name": text(sym.name), "ordinal": sym.ordinal, "rva": hx(sym.address)})
    debug = []
    for item in getattr(pe, "DIRECTORY_ENTRY_DEBUG", []) or []:
        debug.append({
            "type": item.struct.Type,
            "rva": hx(item.struct.AddressOfRawData),
            "size": item.struct.SizeOfData,
        })
    overlay_start = pe.get_overlay_data_start_offset()
    load_config = directory(pe, d["IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG"])
    security = directory(pe, d["IMAGE_DIRECTORY_ENTRY_SECURITY"])
    dll_flags = oh.DllCharacteristics
    report = {
        "schema_version": 1,
        "audited_path": expected["repository_path"],
        "sha256": digest,
        "size_bytes": len(payload),
        "dos": {"magic": "MZ", "pe_header_file_offset": hx(pe.DOS_HEADER.e_lfanew)},
        "coff": {
            "machine": hx(fh.Machine), "sections": fh.NumberOfSections,
            "timestamp_unix_untrusted": fh.TimeDateStamp,
            "characteristics": hx(fh.Characteristics),
            "optional_header_size": fh.SizeOfOptionalHeader,
            "image_is_dll": bool(fh.Characteristics & 0x2000),
            "large_address_aware": bool(fh.Characteristics & 0x20),
        },
        "optional_header": {
            "magic": hx(oh.Magic), "image_base": hx(oh.ImageBase),
            "entry_point_rva": hx(oh.AddressOfEntryPoint),
            "entry_point_preferred_va": hx(oh.ImageBase + oh.AddressOfEntryPoint),
            "size_of_image": oh.SizeOfImage, "size_of_headers": oh.SizeOfHeaders,
            "section_alignment": oh.SectionAlignment, "file_alignment": oh.FileAlignment,
            "subsystem": oh.Subsystem, "dll_characteristics": hx(dll_flags),
            "dynamic_base": bool(dll_flags & 0x40),
            "nx_compat": bool(dll_flags & 0x100),
            "no_seh": bool(dll_flags & 0x400),
            "high_entropy_va": bool(dll_flags & 0x20),
            "guard_cf_flag": bool(dll_flags & 0x4000),
            "stack_reserve": oh.SizeOfStackReserve,
            "stack_commit": oh.SizeOfStackCommit,
            "heap_reserve": oh.SizeOfHeapReserve,
            "checksum_stored": hx(oh.CheckSum),
            "checksum_computed": hx(pe.generate_checksum()),
        },
        "sections": sections,
        "data_directories": {
            text(item.name): {"address": hx(item.VirtualAddress), "size": item.Size}
            for item in oh.DATA_DIRECTORY
        },
        "imports": imports(pe, "DIRECTORY_ENTRY_IMPORT"),
        "delay_imports": imports(pe, "DIRECTORY_ENTRY_DELAY_IMPORT"),
        "exports": exports,
        "relocations": {
            "directory": directory(pe, d["IMAGE_DIRECTORY_ENTRY_BASERELOC"]),
            "blocks": len(relocation_entries),
            "entry_types": dict(reloc_types),
            "relocations_stripped_flag": bool(fh.Characteristics & 0x1),
        },
        "tls": tls,
        "debug": debug,
        "load_config_directory": load_config,
        "security_directory": security,
        "security_directory_is_file_offset_not_rva": True,
        "certificate_present": security["size"] > 0,
        "overlay": {
            "start_file_offset": hx(overlay_start) if overlay_start is not None else None,
            "size": len(payload) - overlay_start if overlay_start is not None else 0,
        },
        "version_resource": version_info(pe),
        "vendor_marker_counts_nonproof": {
            marker: payload.lower().count(marker.encode("ascii").lower())
            for marker in ("whitemane", "frostmourne")
        },
        "limitations": [
            "Static PE audit only; no runtime loading, anti-cheat probing, code diff or hook validation.",
            "COFF timestamp is not a reliable provenance or compilation-date guarantee.",
            "DLL imports reflect static import tables, not all dynamically loaded libraries.",
            "Certificate presence does not establish Authenticode signature validity.",
            "Text markers alone cannot establish proprietary Whitemane code modifications.",
        ],
    }
    pe.close()
    return report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--path", default=None)
    parser.add_argument("--output", default="dist/reference-client-pe-audit.json")
    args = parser.parse_args()
    expected = json.loads(METADATA.read_text(encoding="utf-8"))
    path = Path(args.path) if args.path else ROOT / expected["repository_path"]
    report = audit(path, expected)
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("REFERENCE PE AUDIT: PASS")
    print("FROSTMOURNE_PE_AUDIT_JSON=" + json.dumps(report, separators=(",", ":"), ensure_ascii=True))


if __name__ == "__main__":
    main()
