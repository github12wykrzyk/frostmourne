"""Exact-fingerprint, read-only investigation of the actual 12340 Lua registry.

A Lua registration is not a native action adapter. The report deliberately
refuses to equate a name/prologue match with safe thread/call semantics.
"""
from __future__ import annotations
import hashlib
import json
import struct
from pathlib import Path
import pefile
import capstone

ROOT = Path(__file__).resolve().parents[1]
METADATA = ROOT / "reference/client/reference.json"
LOOKUP = (
    "CastSpellByName", "CastSpellByID", "SpellTargetUnit",
    "TargetUnit", "TargetNearestEnemy", "TargetLastTarget",
    "UnitGUID", "UnitExists", "UnitCanAttack", "UnitIsDead",
    "UnitAffectingCombat", "UnitClass", "IsStealthed",
    "IsSpellInRange", "IsUsableSpell", "GetSpellCooldown",
    "CheckInteractDistance", "UnitCreatureType",
    "UnitIsFriend", "UnitIsEnemy", "UnitReaction",
    "IsSpellKnown", "GetSpellInfo", "SpellIsTargeting",
)

def main() -> None:
    meta = json.loads(METADATA.read_text(encoding="utf-8"))
    binary = ROOT / meta["repository_path"]
    data = binary.read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    if (sha != meta["sha256"] or len(data) != meta["size_bytes"]
        or meta["build"] != 12340 or meta["architecture"] != "x86"):
        raise SystemExit("FAIL: exact reference fingerprint mismatch")
    pe = pefile.PE(data=data, fast_load=True)
    assert pe.FILE_HEADER.Machine == 0x14c
    assert pe.OPTIONAL_HEADER.Magic == 0x10b
    base = pe.OPTIONAL_HEADER.ImageBase
    def sec(va: int):
        return next((s for s in pe.sections
                     if s.VirtualAddress <= va-base <
                     s.VirtualAddress + max(s.Misc_VirtualSize,s.SizeOfRawData)), None)
    def name(section):
        return section.Name.rstrip(b"\0").decode("ascii", "replace") if section else None
    def read(va: int, length: int) -> bytes:
        s = sec(va)
        if not s: return b""
        off = va-base-s.VirtualAddress
        if off >= s.SizeOfRawData: return b""
        start = s.PointerToRawData + off
        return data[start:start+min(length, s.SizeOfRawData-off)]
    writable_sections = [s for s in pe.sections
                         if name(s) in (".data", ".rdata")]
    code = capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    code.detail = False
    report = {
        "schema_version": 1, "client_sha256": sha,
        "client_build": 12340, "arch": "PE32/I386",
        "method": "static Lua string and function table pointer correspondence",
        "registration_candidates": {},
        "validation": {
            "lua_registry_pairs": "STATIC_ONLY",
            "client_object_manager": "NOT_VALIDATED",
            "eligible_nearby_npc_enumeration": "NOT_VALIDATED",
            "safe_game_action_thread": "NOT_VALIDATED",
            "cast_guid_without_target_change": "NOT_VALIDATED",
            "spell_success_event_correlation": "NOT_VALIDATED",
            "gameplay_action": "NOT_IMPLEMENTED",
        }
    }
    for api in LOOKUP:
        needle = api.encode("ascii")+b"\0"
        matches = []
        begin = 0
        while True:
            ix = data.find(needle, begin)
            if ix == -1: break
            begin = ix + len(needle)
            try: name_va = base+pe.get_rva_from_offset(ix)
            except Exception: continue
            if name(sec(name_va)) not in (".rdata", ".data"): continue
            for s in writable_sections:
                raw = data[s.PointerToRawData:s.PointerToRawData+s.SizeOfRawData]
                ptr = struct.pack("<I",name_va)
                off = 0
                while True:
                    offset = raw.find(ptr,off)
                    if offset == -1: break
                    off = offset+1
                    if offset+8 > len(raw): continue
                    fn_va = struct.unpack_from("<I",raw,offset+4)[0]
                    if name(sec(fn_va)) != ".text": continue
                    beginning = read(fn_va, 20)
                    ins = list(code.disasm(beginning,fn_va,count=5))
                    if not ins: continue
                    matches.append({
                        "name_va":f"0x{name_va:08x}",
                        "registration_va":f"0x{base+s.VirtualAddress+offset:08x}",
                        "function_va":f"0x{fn_va:08x}",
                        "prologue_12":beginning[:12].hex(),
                        "instructions":[f"{i.mnemonic} {i.op_str}".strip() for i in ins],
                    })
        report["registration_candidates"][api] = matches[:8]
        print(f"AP_REGISTRY {api} count={len(matches)} " +
              "; ".join(f"table={m['registration_va']} function={m['function_va']}"
                        for m in matches[:3]))
    dest = ROOT/"dist"
    dest.mkdir(exist_ok=True)
    out = dest/"auto-pickpocket-static-audit.json"
    out.write_text(json.dumps(report,indent=2,sort_keys=True)+"\n",encoding="utf-8")
    print("AP_AUDIT client_sha256="+sha)
    print("AP_AUDIT action_adapter=NOT_VALIDATED; actual_pickpocket=NOT_IMPLEMENTED")
    print("AP_AUDIT report="+str(out))
    pe.close()

if __name__ == "__main__":
    main()
