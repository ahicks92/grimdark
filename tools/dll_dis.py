"""Disassemble an exported function of Engine.dll / Game.dll straight from the file on disk (they are not
packed), annotating calls/jumps to other exports of the same DLL and rip-relative data/import references.
Usage: uv run --with pefile --with capstone tools/dll_dis.py <Engine.dll|Game.dll> <undecorated-name-substring> [count=120]
       (the substring must match exactly one export; add parameter text to disambiguate overloads)
"""
import bisect, ctypes, os, re, struct, sys
from gamepath import X64
import capstone, pefile

G = X64
dbg = ctypes.windll.dbghelp


def und(n):
    b = ctypes.create_string_buffer(2048)
    return b.value.decode() if dbg.UnDecorateSymbolName(n.encode(), b, 2048, 0x1000 | 0x2 | 0x4 | 0x8 | 0x10 | 0x20 | 0x80 | 0x100) else n


def main():
    dll, needle = sys.argv[1], sys.argv[2]
    count = int(sys.argv[3], 0) if len(sys.argv) > 3 else 120
    pe = pefile.PE(os.path.join(G, dll), fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXPORT']])
    # pefile caps exports at 8192 by default; Game.dll has 25100 (see gen_exports.py)
    if len(pe.DIRECTORY_ENTRY_EXPORT.symbols) >= 8192:
        pe = pefile.PE(os.path.join(G, dll), fast_load=True, max_symbol_exports=40000)
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXPORT']])
    syms = [(s.address, s.name.decode()) for s in pe.DIRECTORY_ENTRY_EXPORT.symbols if s.name]
    syms.sort()
    rvas = [a for a, _ in syms]
    undec = {a: und(n) for a, n in syms}
    hits = [(a, undec[a]) for a, _ in syms if needle in undec[a]]
    if needle.startswith('0x'): hits = [(a, undec[a]) for a, _ in syms if a == int(needle, 16)]   # exact RVA (disambiguates same-named overloads)
    if needle.startswith('0x') and not hits: hits = [(int(needle, 16), f'<raw {needle}>')]   # an unexported body reached by a forwarder
    if len(hits) != 1:
        print(f"{len(hits)} matches for {needle!r}:")
        for a, u in hits[:20]: print(f"  {a:#x} {u[:160]}")
        return
    rva, name = hits[0]
    base = pe.OPTIONAL_HEADER.ImageBase
    img = pe.get_memory_mapped_image()
    # imports by IAT slot RVA
    imports = {}
    if hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
        for d in pe.DIRECTORY_ENTRY_IMPORT:
            for i in d.imports:
                if i.address: imports[i.address - base] = f"{d.dll.decode()}!{i.name.decode() if i.name else i.ordinal}"

    def sym_at(target_rva):
        if target_rva in imports: return "-> " + imports[target_rva]
        i = bisect.bisect_right(rvas, target_rva) - 1
        if i >= 0 and target_rva - rvas[i] < 0x4000:
            off = target_rva - rvas[i]
            return f"{undec[rvas[i]][:90]}" + (f"+{off:#x}" if off else "")
        return ""

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    print(f"=== {dll} {rva:#x}: {name}")
    for n, ins in enumerate(md.disasm(bytes(img[rva:rva + count * 16]), base + rva)):
        if n >= count: break
        extra = ""
        m = re.search(r"\[rip ([+-]) (0x[0-9a-f]+)\]", ins.op_str)
        if m:
            tgt = ins.address + ins.size + (int(m.group(2), 16) * (1 if m.group(1) == "+" else -1)) - base
            extra = f"   ; [{dll}+{tgt:#x}] {sym_at(tgt)}"
        elif ins.mnemonic in ("call", "jmp") and ins.op_str.startswith("0x"):
            tgt = int(ins.op_str, 16) - base
            extra = f"   ; {dll}+{tgt:#x} {sym_at(tgt)}"
        print(f"  +{ins.address - base:#x}: {ins.mnemonic} {ins.op_str}{extra}")
        if ins.mnemonic == "ret" and n > 4: break


if __name__ == "__main__":
    main()
