"""Cross-references inside Engine.dll / Game.dll (on disk, unpacked): every instruction whose RIP-relative operand
or direct call/jmp target is one of the given RVAs, labelled with the nearest preceding export (+offset), plus
the .pdata function start containing it. Also resolves import slots and finds ASCII strings.
Usage: uv run --with pefile --with capstone tools/dll_xref.py <Engine.dll|Game.dll> <rva-hex> [<rva-hex> ...]
       uv run --with pefile --with capstone tools/dll_xref.py <dll> str <text>      ASCII strings containing text, with xrefs
       uv run --with pefile --with capstone tools/dll_xref.py <dll> imp <rva-hex>   name of the import slot at rva
       uv run --with pefile --with capstone tools/dll_xref.py <dll> fn <rva-hex>    .pdata bounds of the function containing rva
       uv run --with pefile --with capstone tools/dll_xref.py <dll> dis <rva-hex> [count]   disassemble at an arbitrary rva (labels, strings)
Disassembling all of .text takes ~20 s for Game.dll; results are cached per dll in build/xref_<dll>.txt.
"""
import bisect, ctypes, os, struct, sys
from gamepath import X64
import capstone, pefile

G = X64
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
dbg = ctypes.windll.dbghelp
def und(n):
    b = ctypes.create_string_buffer(2048)
    return b.value.decode() if dbg.UnDecorateSymbolName(n.encode(), b, 2048, 0x1000 | 0x2 | 0x4 | 0x8 | 0x10 | 0x20 | 0x80 | 0x100) else n

dll = sys.argv[1]
pe = pefile.PE(os.path.join(G, dll), fast_load=True, max_symbol_exports=40000)
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXPORT'],
                                       pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT'],
                                       pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXCEPTION']])
img = pe.get_memory_mapped_image()
exports = sorted((s.address, und(s.name.decode())) for s in pe.DIRECTORY_ENTRY_EXPORT.symbols if s.name)
exp_addrs = [a for a, _ in exports]
imports = {}
for e in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
    for i in e.imports:
        if i.name: imports[i.address - pe.OPTIONAL_HEADER.ImageBase] = e.dll.decode() + "!" + und(i.name.decode())
funcs = []
for rf in getattr(pe, "DIRECTORY_ENTRY_EXCEPTION", []):
    funcs.append((rf.struct.BeginAddress, rf.struct.EndAddress))
funcs.sort(); func_starts = [a for a, _ in funcs]

def label(rva):
    i = bisect.bisect_right(exp_addrs, rva) - 1
    if i < 0: return "?"
    a, n = exports[i]
    return n + ("" if a == rva else "+%#x" % (rva - a))
def fn_of(rva):
    i = bisect.bisect_right(func_starts, rva) - 1
    if i >= 0 and funcs[i][0] <= rva < funcs[i][1]: return funcs[i]
    return None
def cstr(rva, limit=120):
    e = img.find(b"\0", rva, rva + limit)
    return img[rva:e if e >= 0 else rva + limit].decode("ascii", errors="replace")

def text_sections():
    for s in pe.sections:
        if s.Characteristics & 0x20000000: yield s.VirtualAddress, s.Misc_VirtualSize

def build_cache():
    path = os.path.join(ROOT, "build", "xref_" + dll + ".txt")
    if os.path.exists(path) and os.path.getmtime(path) > os.path.getmtime(os.path.join(G, dll)): return path
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64); md.detail = True
    out = open(path, "w")
    for va, size in text_sections():
        code = img[va:va + size]
        off = 0
        while off < size:
            n = 0
            for ins in md.disasm(code[off:off + 0x100000], va + off):
                n += 1
                tgt = None
                for op in ins.operands:
                    if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                        tgt = ins.address + ins.size + op.mem.disp
                    elif op.type == capstone.x86.X86_OP_IMM and ins.mnemonic.startswith(("call", "j")):
                        tgt = op.imm
                if tgt is not None: out.write("%x %x %s %s\n" % (tgt, ins.address, ins.mnemonic, ins.op_str))
                last = ins.address + ins.size
            if n == 0: off += 1
            else:
                if last - va >= size: break
                off = last - va
                if off < size and n < 2: off += 1
    out.close()
    return path

def xrefs(targets):
    path = build_cache()
    want = set(targets)
    for line in open(path):
        t, a, rest = line.split(" ", 2)
        t = int(t, 16)
        if t in want:
            a = int(a, 16)
            f = fn_of(a)
            fs = (" fn %s" % label(f[0])) if f else ""
            extra = ""
            if t in imports: extra = "   ; import " + imports[t]
            print("  -> %#x: %s+%#x: %s%s%s" % (t, dll, a, rest.strip(), extra, "   [" + label(a) + fs + "]"))

args = sys.argv[2:]
if args and args[0] == "str":
    text = " ".join(args[1:]).encode()
    hits = []
    i = img.find(text)
    while i >= 0:
        s = i
        while s > 0 and 0x20 <= img[s - 1] < 0x7f: s -= 1
        hits.append(s); i = img.find(text, i + 1)
    hits = sorted(set(hits))
    for h in hits: print("%s+%#x: %r" % (dll, h, cstr(h)))
    xrefs(hits)
elif args and args[0] == "imp":
    for a in args[1:]: r = int(a, 16); print("%#x: %s" % (r, imports.get(r, "(not an import slot)")))
elif args and args[0] == "dis":
    rva = int(args[1], 16); count = int(args[2]) if len(args) > 2 else 40
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64); md.detail = True
    print("=== %s+%#x (%s)" % (dll, rva, label(rva)))
    for ins in md.disasm(img[rva:rva + 16 * count], rva):
        note = ""
        for op in ins.operands:
            tgt = None
            if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP: tgt = ins.address + ins.size + op.mem.disp
            elif op.type == capstone.x86.X86_OP_IMM and ins.mnemonic.startswith(("call", "j")): tgt = op.imm
            if tgt is not None:
                if tgt in imports: note = "   ; import " + imports[tgt]
                else:
                    note = "   ; %s+%#x %s" % (dll, tgt, label(tgt))
                    b = img[tgt:tgt + 4]
                    if len(b) == 4 and all(0x20 <= c < 0x7f for c in b): note += " %r" % cstr(tgt, 60)
        print("  +%#x: %s %s%s" % (ins.address, ins.mnemonic, ins.op_str, note))
        count -= 1
        if count <= 0: break
elif args and args[0] == "fn":
    for a in args[1:]:
        f = fn_of(int(a, 16)); print("%s: %s" % (a, ("%#x..%#x %s" % (f[0], f[1], label(f[0]))) if f else "no .pdata entry"))
else:
    xrefs([int(a, 16) for a in args])
