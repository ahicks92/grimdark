"""Find the low-level keyboard hook in DirectInput.dll and dump what VKs it compares against."""
import pefile, capstone, struct, re
import os
from gamepath import X64
P = os.path.join(X64, "DirectInput.dll")
pe = pefile.PE(P)
base = pe.OPTIONAL_HEADER.ImageBase
text = next(s for s in pe.sections if s.Name.startswith(b".text"))
code = text.get_data(); start = base + text.VirtualAddress
# import thunk addresses
iat = {}
for e in pe.DIRECTORY_ENTRY_IMPORT:
    for i in e.imports:
        if i.name: iat[i.address] = i.name.decode()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64); md.detail = True
insns = list(md.disasm(code, start))
byaddr = {i.address: k for k, i in enumerate(insns)}
def imm_target(i):
    m = re.search(r"\[rip \+ (0x[0-9a-f]+)\]", i.op_str)
    if m: return i.address + i.size + int(m.group(1), 16)
    m = re.search(r"\[rip - (0x[0-9a-f]+)\]", i.op_str)
    if m: return i.address + i.size - int(m.group(1), 16)
    return None
# 1) SetWindowsHookExA call site: look back for idHook (ecx) and lpfn (rdx = lea rip-rel)
for k, i in enumerate(insns):
    if i.mnemonic == "call":
        t = imm_target(i)
        if t in iat and iat[t] in ("SetWindowsHookExA", "SystemParametersInfoA", "UnhookWindowsHookEx"):
            print(f"\n{iat[t]} called at {i.address:#x}")
            for j in insns[max(0, k-14):k]:
                print(f"   {j.address:#x}: {j.mnemonic} {j.op_str}")
            if iat[t] == "SetWindowsHookExA":
                for j in insns[max(0, k-14):k]:
                    if j.mnemonic == "lea" and j.op_str.startswith("rdx"):
                        hp = imm_target(j); print(f"   => hook proc at {hp:#x}")
                        # dump the hook proc: all cmp/test/mov with small immediates until ret
                        kk = byaddr.get(hp)
                        if kk is None: continue
                        print("   hook proc body (cmp/jcc/imm only):")
                        n = 0
                        for j2 in insns[kk:kk+400]:
                            if j2.mnemonic in ("cmp", "test", "sub") or (j2.mnemonic == "mov" and re.search(r", 0x[0-9a-f]+$", j2.op_str)) or j2.mnemonic.startswith("j") or j2.mnemonic in ("call","ret"):
                                print(f"      {j2.address:#x}: {j2.mnemonic} {j2.op_str}")
                            n += 1
                            if j2.mnemonic == "ret" and n > 5: break
