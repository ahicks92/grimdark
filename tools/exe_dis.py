"""Disassemble / cross-reference the dumped (unpacked) exe image, annotating calls into Engine.dll/Game.dll by
export name (from the dump's own import directory -- no live game needed) and RIP-relative string operands.
Usage: uv run --with pefile --with capstone tools/exe_dis.py <rva-hex> [count] [<rva-hex> [count] ...]
       uv run --with pefile --with capstone tools/exe_dis.py xref <rva-hex>       RIP-relative refs + direct calls/jmps to rva
       uv run --with pefile --with capstone tools/exe_dis.py str <text>           find ASCII/UTF-16 strings containing text, with xrefs
       uv run --with pefile --with capstone tools/exe_dis.py imp <regex>          list imports (IAT slot rva, name) matching regex
       uv run --with pefile --with capstone tools/exe_dis.py ptrs <rva-hex> [n]   dump n qwords at rva, annotated (vtables)
       uv run --with pefile --with capstone tools/exe_dis.py fn <rva-hex>         disassemble until the first ret past the entry
Needs build/GrimDawn.unpacked.bin (+ .base) from tools/dump_exe.py. RVAs are hex, with or without 0x.
"""
import bisect, ctypes, ctypes.wintypes as wt, os, re, struct, subprocess, sys
from gamepath import X64
import capstone, pefile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
G = X64
img = open(os.path.join(ROOT, "build", "GrimDawn.unpacked.bin"), "rb").read()
base = int(open(os.path.join(ROOT, "build", "GrimDawn.unpacked.bin.base")).read().strip(), 16)

dbg = ctypes.windll.dbghelp
def und(n):
    b = ctypes.create_string_buffer(1024)
    return b.value.decode() if dbg.UnDecorateSymbolName(n.encode(), b, 1024, 0x1000 | 0x2 | 0x4 | 0x8 | 0x10 | 0x20 | 0x80 | 0x100) else n

# --- the dump's import directory: IAT slot rva -> "dll!undecorated name" (image layout: offsets == rvas) ---
imports = {}
def parse_imports():
    e_lfanew = struct.unpack_from("<I", img, 0x3c)[0]
    opt = e_lfanew + 0x18
    magic = struct.unpack_from("<H", img, opt)[0]
    dd = opt + (0x70 if magic == 0x20b else 0x60) + 8 * 1  # data directory 1 = imports
    irva, isize = struct.unpack_from("<II", img, dd)
    if not irva: return
    d = irva
    while True:
        oft, _, _, name_rva, ft = struct.unpack_from("<IIIII", img, d)
        if not oft and not ft: break
        dll = img[name_rva:img.index(b"\0", name_rva)].decode(errors="replace")
        names = oft or ft
        k = 0
        while True:
            v = struct.unpack_from("<Q", img, names + 8 * k)[0]
            if not v: break
            if v >> 63: nm = f"#{v & 0xffff}"
            else:
                hn = v & 0xffffffff
                nm = img[hn + 2:img.index(b"\0", hn + 2)].decode(errors="replace")
            imports[ft + 8 * k] = f"{dll}!{und(nm)[:90]}"
            k += 1
        d += 20
try:
    parse_imports()
except Exception as e:
    print("(import directory not parsed:", e, ")")

# live module bases (only for resolving pointers that are not IAT slots)
mods = {}
try:
    k32 = ctypes.WinDLL("kernel32"); psapi = ctypes.WinDLL("psapi"); k32.OpenProcess.restype = wt.HANDLE
    pid = int(subprocess.check_output(["powershell", "-NoProfile", "-Command", "(Get-Process 'Grim Dawn' -ErrorAction SilentlyContinue).Id"],
                                      stderr=subprocess.DEVNULL).decode().strip())
    proc = k32.OpenProcess(0x1F0FFF, False, pid)
    hm = (ctypes.c_void_p * 2048)(); needed = wt.DWORD()
    psapi.EnumProcessModulesEx(proc, hm, ctypes.sizeof(hm), ctypes.byref(needed), 3)
    for i in range(needed.value // 8):
        buf = ctypes.create_unicode_buffer(260); psapi.GetModuleFileNameExW(proc, ctypes.c_void_p(hm[i]), buf, 260)
        mods[os.path.basename(buf.value).lower()] = hm[i]
except Exception:
    pass

exports = {}
for dll in ("Engine.dll", "Game.dll", "DirectInput.dll"):
    try:
        pe = pefile.PE(os.path.join(G, dll), fast_load=True)
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]])
        exports[dll.lower()] = sorted((s.address, s.name.decode()) for s in pe.DIRECTORY_ENTRY_EXPORT.symbols if s.name)
    except Exception:
        pass

def modname(addr):
    if base <= addr < base + len(img): return f"exe+{addr - base:#x}"
    best = None
    for name, b in mods.items():
        if b <= addr and (best is None or b > mods[best]): best = name
    if best is None: return f"{addr:#x}"
    off = addr - mods[best]
    if best in exports:
        rvas = [a for a, _ in exports[best]]
        i = bisect.bisect_right(rvas, off) - 1
        if i >= 0 and off - rvas[i] < 0x2000:
            return f"{best}!{und(exports[best][i][1])[:70]}+{off - rvas[i]:#x}"
    return f"{best}+{off:#x}"

def string_at(rva, limit=80):
    """Printable ASCII or UTF-16LE string at rva, or None."""
    if not (0 <= rva < len(img)): return None
    s = img[rva:rva + limit * 2]
    m = re.match(rb"[\x20-\x7e]{4,}", s)
    if m: return '"' + m.group(0).decode()[:limit] + '"'
    m = re.match(rb"(?:[\x20-\x7e]\x00){4,}", s)
    if m: return 'L"' + m.group(0).decode("utf-16le")[:limit] + '"'
    return None

def annotate_target(ins, trva):
    """Annotation for an instruction whose RIP-relative operand resolves to image rva trva."""
    if trva in imports: return "   ; -> " + imports[trva]
    if ins.mnemonic in ("call", "jmp") and 0 <= trva < len(img) - 8:
        return "   ; -> " + modname(struct.unpack_from("<Q", img, trva)[0])
    s = string_at(trva)
    if s: return f"   ; [exe+{trva:#x}] {s}"
    if 0 <= trva < len(img) - 8:
        q = struct.unpack_from("<Q", img, trva)[0]
        if base <= q < base + len(img): return f"   ; [exe+{trva:#x}] = exe+{q - base:#x}"
    return f"   ; [exe+{trva:#x}]"

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
def fmt(ins):
    extra = ""
    m = re.search(r"\[rip ([+-]) (0x[0-9a-f]+)\]", ins.op_str)
    if m:
        t = ins.address + ins.size + (int(m.group(2), 16) if m.group(1) == "+" else -int(m.group(2), 16))
        extra = annotate_target(ins, t - base)
    elif ins.mnemonic in ("call", "jmp") and ins.op_str.startswith("0x"):
        extra = f"   ; exe+{int(ins.op_str, 16) - base:#x}"
    elif ins.mnemonic.startswith("j") and ins.op_str.startswith("0x"):
        extra = f"   ; exe+{int(ins.op_str, 16) - base:#x}"
    return f"  +{ins.address - base:#x}: {ins.mnemonic} {ins.op_str}{extra}"

def dis(rva, count):
    print(f"=== exe+{rva:#x} ({base + rva:#x}) ===")
    for i, ins in enumerate(md.disasm(img[rva:rva + count * 16], base + rva)):
        print(fmt(ins))
        if i >= count: break

def dis_fn(rva, max_ins=2000):
    """Disassemble to the first ret that is followed by padding / a new function (int3 or a prologue)."""
    print(f"=== exe+{rva:#x} ({base + rva:#x}) function ===")
    n = 0
    for ins in md.disasm(img[rva:rva + max_ins * 16], base + rva):
        print(fmt(ins)); n += 1
        if ins.mnemonic == "ret" or n >= max_ins:
            nxt = img[ins.address - base + ins.size]
            if nxt == 0xcc or n >= max_ins: break
        if ins.mnemonic == "int3": break

def xrefs(target):
    """All instructions with a RIP-relative operand or a direct call/jmp resolving to target."""
    hits = []
    # RIP-relative: disp32 is the last 4 bytes of the instruction (no immediate) or before a 1/4-byte immediate.
    # Brute force: look for disp32 values d where (pos + 4 + imm_len) + d == target for imm_len in 0,1,4.
    for imm_len in (0, 1, 4):
        for pos in range(0, len(img) - 4):
            d = target - (pos + 4 + imm_len)
            if -0x80000000 <= d <= 0x7fffffff:
                if img[pos:pos + 4] == struct.pack("<i", d):
                    hits.append(pos)
    # direct call/jmp rel32 (E8/E9)
    for pos in range(0, len(img) - 5):
        if img[pos] in (0xe8, 0xe9):
            d = struct.unpack_from("<i", img, pos + 1)[0]
            if pos + 5 + d == target: hits.append(pos)
    out = []
    for pos in sorted(set(hits)):
        # disassemble a window ending after pos to find the instruction containing it
        start = max(0, pos - 12)
        for ins in md.disasm(img[start:pos + 16], base + start):
            a = ins.address - base
            if a <= pos < a + ins.size:
                if ins.mnemonic in ("call", "jmp") and ins.op_str.startswith("0x") and int(ins.op_str, 16) - base != target: break
                if "rip" in ins.op_str or ins.mnemonic in ("call", "jmp"):
                    out.append(fmt(ins))
                break
    return out

def find_strings(text):
    res = []
    for m in re.finditer(re.escape(text.encode()), img):
        s = m.start()
        while s > 0 and 0x20 <= img[s - 1] <= 0x7e: s -= 1
        res.append(s)
    w = text.encode("utf-16le")
    for m in re.finditer(re.escape(w), img):
        s = m.start()
        while s > 1 and 0x20 <= img[s - 2] <= 0x7e and img[s - 1] == 0: s -= 2
        res.append(s)
    return sorted(set(res))

def rva_of(s): return int(s, 16)

args = sys.argv[1:]
if not args:
    print(__doc__); sys.exit(0)
if args[0] == "xref":
    t = rva_of(args[1])
    for line in xrefs(t): print(line)
elif args[0] == "str":
    for rva in find_strings(" ".join(args[1:])):
        print(f"exe+{rva:#x}: {string_at(rva, 120)}")
        for line in xrefs(rva)[:40]: print("   ", line)
elif args[0] == "imp":
    pat = re.compile(" ".join(args[1:]), re.I)
    for rva, name in sorted(imports.items()):
        if pat.search(name): print(f"exe+{rva:#x}: {name}")
elif args[0] == "ptrs":
    rva = rva_of(args[1]); n = int(args[2]) if len(args) > 2 else 32
    for k in range(n):
        q = struct.unpack_from("<Q", img, rva + 8 * k)[0]
        note = modname(q) if q else "0"
        if base <= q < base + len(img):
            s = string_at(q - base)
            if s: note += " " + s
        print(f"  +{rva + 8 * k:#x} [{k}]: {q:#x}  {note}")
elif args[0] == "fn":
    dis_fn(rva_of(args[1]))
else:
    i = 0
    while i < len(args):
        rva = rva_of(args[i]); count = 60
        if i + 1 < len(args) and args[i + 1].isdigit(): count = int(args[i + 1]); i += 1
        dis(rva, count); i += 1
