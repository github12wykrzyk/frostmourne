"""Static research only, exact registered WoW 3.3.5a/12340 x86 executable."""
import hashlib, json, struct
from pathlib import Path
import pefile, capstone

root = Path(__file__).resolve().parents[1]
meta = json.loads((root/"reference/client/reference.json").read_text(encoding="utf8"))
data = (root/meta["repository_path"]).read_bytes()
sha = hashlib.sha256(data).hexdigest()
assert sha == meta["sha256"] and len(data) == meta["size_bytes"] and meta["build"] == 12340
pe = pefile.PE(data=data,fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
assert pe.FILE_HEADER.Machine == 0x14c and pe.OPTIONAL_HEADER.Magic == 0x10b and base == 0x400000
cs = capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
cs.detail = True

def section(va):
    rva = va-base
    return next((s for s in pe.sections if s.VirtualAddress <= rva < s.VirtualAddress+max(s.Misc_VirtualSize,s.SizeOfRawData)),None)
def secname(s):
    return s.Name.rstrip(b"\0").decode("ascii","replace") if s else "UNMAPPED"
def read(va,count):
    s=section(va)
    if not s: return b""
    o=va-base-s.VirtualAddress
    if o>=s.SizeOfRawData: return b""
    off=s.PointerToRawData+o
    return data[off:off+min(count,s.SizeOfRawData-o)]
def dw(va):
    b=read(va,4)
    return struct.unpack("<I",b)[0] if len(b)==4 else None
def refs(value,sections=None,limit=12):
    v=struct.pack("<I",value)
    out=[]
    for s in pe.sections:
        name=secname(s)
        if sections and name not in sections: continue
        raw=data[s.PointerToRawData:s.PointerToRawData+s.SizeOfRawData]
        pos=raw.find(v)
        while pos!=-1:
            out.append((base+s.VirtualAddress+pos,name))
            if len(out)>=limit:return out
            pos=raw.find(v,pos+1)
    return out
def ins(va,length,limit=140):
    out=[]
    for i in cs.disasm(read(va,length),va):
        out.append(i)
        if len(out)>=limit: break
    return out
def registration(name):
    print("\nLUA_NAME",name)
    needle=name.encode()+b"\0"
    pos=data.find(needle)
    if pos<0: print("NOT_FOUND");return
    va=base+pe.get_rva_from_offset(pos)
    print("STRING_VA",hex(va),"SECTION",secname(section(va)))
    locations=refs(va,{".rdata",".data",".text"},20)
    print("POINTER_REFS",[(hex(p),n) for p,n in locations])
    for p,n in locations:
        following=dw(p+4)
        print("POTENTIAL_REGISTER_PAIR",hex(p),n,"NEXT_DWORD",hex(following) if following is not None else "?",
              "NEXT_SECTION",secname(section(following)) if following else "?")
        if following and secname(section(following))==".text":
            print("POSSIBLE_FUNCTION_PROLOG",hex(following),read(following,12).hex(" "))
def dump(va,length,limit):
    print("\nDISASM",hex(va),"SECTION",secname(section(va)),"RAW",read(va,16).hex(" "))
    for i in ins(va,length,limit):
        marker=[]
        for op in i.operands:
            if op.type==capstone.x86.X86_OP_MEM and op.mem.disp in (0xA6C,0xA70,0xA74,0xA78,0xA7C,0xA80,0xA84,0xA88,0xC08,0xC20):
                marker.append("FIELD_CANDIDATE="+hex(op.mem.disp))
        print(hex(i.address),i.mnemonic,i.op_str,*marker)
def callers(va,maxnum=6):
    s=next(s for s in pe.sections if secname(s)==".text")
    raw=data[s.PointerToRawData:s.PointerToRawData+s.SizeOfRawData]
    textva=base+s.VirtualAddress
    offsets=[]
    pos=raw.find(b"\xe8")
    while pos!=-1:
        if pos+5<=len(raw) and textva+pos+5+struct.unpack_from("<i",raw,pos+1)[0]==va:
            offsets.append(textva+pos)
        pos=raw.find(b"\xe8",pos+1)
    print("\nCALLERS_HEURISTIC",hex(va),"COUNT",len(offsets))
    for p in offsets[:maxnum]:
        print("CALLSITE",hex(p),"PRE_BYTES",read(p-25,25).hex(" "))
        for i in ins(p,45,8):
            print(hex(i.address),i.mnemonic,i.op_str)
print("FROSTMOURNE EXACT 12340 READ-ONLY RESEARCH SHA256",sha)
for n in ("UnitCastingInfo","UnitChannelInfo","CastSpellByID","CastSpellByName","UnitGUID",
          "UnitExists","UnitCanAttack","GetSpellCooldown","IsUsableSpell"):
    registration(n)
for address,length,cap in ((0x611df0,0x295,175),(0x612090,0x205,145),
                           (0x80da40,0x3c,35),(0x80da80,0x142,120),
                           (0x80cce0,0x135,85),
                           (0x53e060,0x200,130),(0x60e630,0x160,95),
                           (0x60c1f0,0x150,90),(0x60abf0,0x1a0,125),
                           (0x84e0e0,0x135,90),(0x86ae20,0x90,40)):
    dump(address,length,cap)
for address in (0x80da40,0x611df0,0x612090,0x80cce0):
    callers(address)
print("\nSTATIC ONLY: No safe game-thread calling convention, unit layout or dynamic state established.")
pe.close()
