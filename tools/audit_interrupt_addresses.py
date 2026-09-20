"""Read-only candidate-address audit against the EXACT registered 3.3.5a x86 image.

A matching byte window or a guessed function name is NOT a verified game API.
No client execution, injection, hooks or runtime changes are performed.
"""
from __future__ import annotations

import hashlib
import json
import struct
from pathlib import Path

import capstone
import pefile

ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / "reference/client/reference.json"
ADDRESS_CANDIDATES = {
    "Script_UnitCastingInfo_claim": 0x00611DF0,
    "Script_UnitChannelInfo_claim": 0x00612090,
    "CGGameUI_CastSpell_claim": 0x0080DA40,
    "Spell_C_CastSpell_claim": 0x0080DB50,
    "Cooldown_claim": 0x00807980,
    "Lua_state_pointer_claim": 0x00D3F78C,
    "Mouseover_GUID_claim": 0x00BD07A0,
    "Last_target_GUID_claim": 0x00BD07B8,
}
FIELD_CANDIDATES = [0xA6C, 0xA78, 0xA7C, 0xA80, 0xA84, 0xA88, 0xC08, 0xC20]
STRINGS = ["UnitCastingInfo", "UnitChannelInfo", "CastSpellByName", "CastSpellByID"]
WINDOW = 0x120


def section_for(pe: pefile.PE, rva: int):
    for s in pe.sections:
        first = s.VirtualAddress
        if first <= rva < first + max(s.Misc_VirtualSize, s.SizeOfRawData):
            return s
    return None


def raw_bytes(data: bytes, section, rva: int, length: int) -> bytes:
    if section is None:
        return b""
    offset_in_section = rva - section.VirtualAddress
    if offset_in_section >= section.SizeOfRawData:
        return b""  # BSS/virtual-only memory has no corresponding file bytes
    start = section.PointerToRawData + offset_in_section
    extent = min(length, section.SizeOfRawData - offset_in_section)
    return data[start:start+extent]


def find_direct_calls(raw_text: bytes, text_va: int, target: int):
    """Heuristic only: E8 bytes need not be instruction boundaries."""
    matches = []
    start = 0
    while True:
        ix = raw_text.find(b"\xe8", start)
        if ix < 0:
            return matches
        if ix + 5 <= len(raw_text):
            dest = text_va + ix + 5 + struct.unpack_from("<i", raw_text, ix + 1)[0]
            if dest == target:
                matches.append(text_va + ix)
        start = ix + 1


def find_file_occurrences(pe, data, text_section, value: int):
    value_bytes = struct.pack("<I", value)
    raw = data[text_section.PointerToRawData:text_section.PointerToRawData+text_section.SizeOfRawData]
    first = 0
    hits = []
    while True:
        pos = raw.find(value_bytes, first)
        if pos < 0:
            break
        hits.append(pe.OPTIONAL_HEADER.ImageBase + text_section.VirtualAddress + pos)
        first = pos + 1
    return hits


def inspect_candidate(name, va, pe, data, text, disassembler):
    rva = va - pe.OPTIONAL_HEADER.ImageBase
    section = section_for(pe, rva)
    section_name = section.Name.rstrip(b"\0").decode("ascii", "replace") if section else "UNMAPPED"
    raw = raw_bytes(data, section, rva, WINDOW)
    print(f"\n=== {name} at VA=0x{va:08X} RVA=0x{rva:08X} section={section_name} ===")
    if not raw:
        print("NO FILE-BACKED BYTES (may be BSS, data or invalid); no execution claim")
        return
    print("FIRST_32_BYTES:", raw[:32].hex(" "))
    if section is not text:
        print("NON_EXECUTABLE_CANDIDATE: inspect as data, not a function.")
        print("POSSIBLE_STATIC_POINTER_VALUE:", f"0x{struct.unpack_from('<I', raw)[0]:08X}" if len(raw) >= 4 else "unknown")
        hits = find_file_occurrences(pe, data, text, va)
        print("RAW_32BIT_ADDRESS_REFERENCES_IN_TEXT:", len(hits), "SAMPLE:", [f"0x{x:08X}" for x in hits[:8]])
        return

    text_raw = data[text.PointerToRawData:text.PointerToRawData+text.SizeOfRawData]
    direct_calls = find_direct_calls(text_raw, pe.OPTIONAL_HEADER.ImageBase + text.VirtualAddress, va)
    print("HEURISTIC_DIRECT_CALLS_TO_EXACT_VA:", len(direct_calls),
          [f"0x{x:08X}" for x in direct_calls[:12]])
    print("DECODED_INSTRUCTIONS_FIRST_0x120_BYTES (linear, boundaries not established):")
    found_fields = set()
    for idx, ins in enumerate(disassembler.disasm(raw, va)):
        if idx >= 68:
            break
        fields = []
        for op in ins.operands:
            if op.type == capstone.x86.X86_OP_MEM:
                disp = op.mem.disp
                if disp in FIELD_CANDIDATES:
                    found_fields.add(disp)
                    fields.append(f"field_candidate=0x{disp:X}")
        print(f"0x{ins.address:08X} {ins.mnemonic:8s} {ins.op_str[:66]}",
              " ".join(fields))
    print("FIELD_DISPLACEMENTS_IN_WINDOW:", [f"0x{v:X}" for v in sorted(found_fields)])


def inspect_strings(pe, data, text):
    imagebase = pe.OPTIONAL_HEADER.ImageBase
    print("\n=== LUA API NAME SEARCH (strings alone are NOT function identification) ===")
    for name in STRINGS:
        found = []
        for enc in ("ascii", "utf-16le"):
            needle = name.encode(enc) + (b"\0\0" if enc == "utf-16le" else b"\0")
            at = 0
            while len(found) < 12:
                at = data.find(needle, at)
                if at < 0:
                    break
                try:
                    rva = pe.get_rva_from_offset(at)
                except Exception:
                    rva = None
                if rva is not None:
                    va = imagebase + rva
                    hits = find_file_occurrences(pe, data, text, va)
                    found.append((enc, f"0x{va:08X}", len(hits), [f"0x{x:08X}" for x in hits[:4]]))
                at += len(needle)
        print(name, "string locations; references in .text:", found)


def main():
    meta = json.loads(REFERENCE.read_text(encoding="utf-8"))
    path = ROOT / meta["repository_path"]
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if (digest != meta["sha256"] or len(data) != meta["size_bytes"]
            or meta["build"] != 12340 or meta["architecture"] != "x86"):
        raise SystemExit("FAIL: reference fingerprint/target mismatch; refuse address audit")
    pe = pefile.PE(data=data, fast_load=True)
    if pe.FILE_HEADER.Machine != 0x14C or pe.OPTIONAL_HEADER.Magic != 0x10B:
        raise SystemExit("FAIL: expected PE32 I386")
    text = next((s for s in pe.sections if s.Name.rstrip(b"\0") == b".text"), None)
    if text is None:
        raise SystemExit("FAIL: missing .text")
    disassembler = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    disassembler.detail = True
    print("FROSTMOURNE READ-ONLY CANDIDATE ADDRESS AUDIT")
    print("fingerprint_sha256:", digest, "size:", len(data),
          "image_base:", f"0x{pe.OPTIONAL_HEADER.ImageBase:08X}")
    print("No semantics confirmed without function registration/call graph and runtime checks.")
    for name, va in ADDRESS_CANDIDATES.items():
        inspect_candidate(name, va, pe, data, text, disassembler)
    inspect_strings(pe, data, text)
    print("\nRESULT: candidate bytes and heuristic references collected; NOT a validated native adapter.")
    pe.close()


if __name__ == "__main__":
    main()
