"""Relocate exe RVAs from one game build to the next by diffing two unpacked dumps (tools/dump_exe.py).

Functions come from each image's .pdata; each is normalized (RIP-relative operands and branch targets masked,
import calls named by import) and matched old -> new: identical normalized bodies with a unique hash first, then
by propagation through matched pairs (call targets and RIP-relative data refs at the same instruction index),
which also yields the data map (vtables, globals). An RVA inside a function maps by instruction alignment.

Usage:
  uv run tools/exe_reloc.py OLD.bin NEW.bin rva [rva ...]       map RVAs (hex)
  uv run tools/exe_reloc.py OLD.bin NEW.bin --scan FILE [...]   map every RVA constant and exe+0x evidence in FILEs
OLD/NEW default to the newest archive dump and build/GrimDawn.unpacked.bin with "-" for either.
"""
import bisect, difflib, hashlib, os, re, struct, sys
from collections import Counter, defaultdict
import capstone
from capstone import x86

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class Image:
    def __init__(self, path):
        self.path = path
        self.img = open(path, "rb").read()
        e = struct.unpack_from("<I", self.img, 0x3c)[0]
        if not path.endswith(".bin"):
            # an on-disk PE (Engine.dll / Game.dll are not packed): lay the sections out by RVA like a dump
            nsec = struct.unpack_from("<H", self.img, e + 6)[0]; sopt = struct.unpack_from("<H", self.img, e + 0x14)[0]
            vimg = bytearray(struct.unpack_from("<I", self.img, e + 0x18 + 0x38)[0])
            hdr = struct.unpack_from("<I", self.img, e + 0x18 + 0x3c)[0]
            vimg[:hdr] = self.img[:hdr]
            for i in range(nsec):
                o = e + 0x18 + sopt + 40 * i
                vsize, va, rsize, rptr = struct.unpack_from("<IIII", self.img, o + 8)
                n = min(rsize, vsize) if vsize else rsize
                vimg[va:va + n] = self.img[rptr:rptr + n]
            self.img = bytes(vimg)
        self.exports = {}
        erva = struct.unpack_from("<I", self.img, e + 0x18 + 0x70)[0]
        if erva:
            nfun, nnames, afun, anames, aords = struct.unpack_from("<IIIII", self.img, erva + 0x14)
            for i in range(nnames):
                nm = self.cstr(struct.unpack_from("<I", self.img, anames + 4 * i)[0])
                ordi = struct.unpack_from("<H", self.img, aords + 2 * i)[0]
                self.exports[nm] = struct.unpack_from("<I", self.img, afun + 4 * ordi)[0]
        self.size = struct.unpack_from("<I", self.img, e + 0x18 + 0x38)[0]
        dd = e + 0x18 + 0x70
        self.ts = struct.unpack_from("<I", self.img, e + 8)[0]
        nsec = struct.unpack_from("<H", self.img, e + 6)[0]
        sopt = struct.unpack_from("<H", self.img, e + 0x14)[0]
        self.sections = []
        for i in range(nsec):
            o = e + 0x18 + sopt + 40 * i
            name = self.img[o:o + 8].rstrip(b"\0").decode(errors="replace")
            vsize, va = struct.unpack_from("<II", self.img, o + 8)
            ch = struct.unpack_from("<I", self.img, o + 36)[0]
            self.sections.append((name, va, vsize, ch))
        prva, psize = struct.unpack_from("<II", self.img, dd + 8 * 3)
        funcs = set()
        for i in range(psize // 12):
            b, en, _ = struct.unpack_from("<III", self.img, prva + 12 * i)
            if b and en > b: funcs.add((b, en))
        self.funcs = sorted(funcs)
        self.starts = [f[0] for f in self.funcs]
        self.imports = {}
        irva = struct.unpack_from("<I", self.img, dd + 8 * 1)[0]
        d = irva
        while irva:
            oft, _, _, name_rva, ft = struct.unpack_from("<IIIII", self.img, d)
            if not oft and not ft: break
            dll = self.cstr(name_rva)
            names = oft or ft; k = 0
            while True:
                v = struct.unpack_from("<Q", self.img, names + 8 * k)[0]
                if not v: break
                nm = f"#{v & 0xffff}" if v >> 63 else self.cstr((v & 0xffffffff) + 2)
                self.imports[ft + 8 * k] = f"{dll}!{nm}"
                k += 1
            d += 20
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64); self.md.detail = True
        self._norm = {}

    def cstr(self, rva):
        return self.img[rva:self.img.index(b"\0", rva)].decode(errors="replace")

    def is_code(self, rva):
        return any(va <= rva < va + vs and ch & 0x20 for _, va, vs, ch in self.sections)

    def func_at(self, rva):
        i = bisect.bisect_right(self.starts, rva) - 1
        if i >= 0 and self.funcs[i][0] <= rva < self.funcs[i][1]: return self.funcs[i]
        return None

    def norm(self, f):
        """(tokens, insn offsets, refs) for function f; refs[i] = list of (kind, target rva) for insn i."""
        if f in self._norm: return self._norm[f]
        b, e = f
        toks, offs, refs = [], [], []
        for ins in self.md.disasm(self.img[b:e], b):
            r = []; t = ins.mnemonic + " " + ins.op_str
            for op in ins.operands:
                if op.type == x86.X86_OP_MEM and op.mem.base == x86.X86_REG_RIP:
                    tgt = ins.address + ins.size + op.mem.disp
                    if tgt in self.imports:
                        t = re.sub(r"\[rip [+-] 0x[0-9a-f]+\]", "[" + self.imports[tgt] + "]", t)
                    else:
                        t = re.sub(r"\[rip [+-] 0x[0-9a-f]+\]", "[RIP]", t); r.append(("data", tgt))
                elif op.type == x86.X86_OP_IMM and (ins.group(x86.X86_GRP_JUMP) or ins.group(x86.X86_GRP_CALL)):
                    tgt = op.imm
                    if b <= tgt < e: t = f"{ins.mnemonic} L{tgt - b:#x}"
                    else: t = f"{ins.mnemonic} EXT"; r.append(("code", tgt))
            toks.append(t); offs.append(ins.address - b); refs.append(r)
        out = (toks, offs, refs)
        self._norm[f] = out
        return out

    def digest(self, f):
        return hashlib.sha1("\n".join(self.norm(f)[0]).encode()).hexdigest()


class Reloc:
    def __init__(self, old, new):
        self.old, self.new = old, new
        self.fmap = {}            # old func -> new func
        self.data_votes = defaultdict(Counter)
        self.code_votes = defaultdict(Counter)
        oh = defaultdict(list); nh = defaultdict(list)
        for f in old.funcs: oh[old.digest(f)].append(f)
        for f in new.funcs: nh[new.digest(f)].append(f)
        for h, fs in oh.items():
            if len(fs) == 1 and len(nh.get(h, ())) == 1: self.fmap[fs[0]] = nh[h][0]
        self.identical = set(self.fmap)
        anchors = dict(self.fmap)
        # Fixpoint over votes: every matched pair votes for the call targets / data refs at its aligned instructions
        # (an identical pair at full weight, a changed pair low), every mapped vtable for its slots; each unanchored
        # old function takes its best-voted candidate, an identical body first. First-come assignment picked wrong
        # twins among the many identical small functions.
        for _ in range(8):
            cv = defaultdict(Counter); dv = defaultdict(Counter)
            for of, nf in self.fmap.items():
                w = 10 if self.same(of, nf) else 1
                for (ok, ot), (nk, nt) in self.aligned_refs(of, nf):
                    if ok != nk: continue
                    (dv if ok == "data" else cv)[ot][nt] += w
            self.data_votes = dv
            self._dkeys = sorted(dv)
            for ot, c in dv.items():
                for a, b in self.slots(ot, c.most_common(1)[0][0]): cv[a][b] += 10
            fm = dict(anchors); claimed = Counter()
            for ot, c in cv.items():
                o2 = old.func_at(ot)
                if not o2 or o2[0] != ot or o2 in anchors: continue
                cands = [(t, n) for t, n in c.items() if new.func_at(t) and new.func_at(t)[0] == t]
                if not cands: continue
                cands.sort(key=lambda tn: (self.same(o2, new.func_at(tn[0])), tn[1]), reverse=True)
                fm[o2] = new.func_at(cands[0][0])
            if fm == self.fmap: break
            self.fmap = fm
        # neighbour deltas from the anchors, for the local similarity fallback in map()
        self._anchor_starts = sorted((o[0], n[0] - o[0]) for o, n in anchors.items())

    def same(self, of, nf):
        return self.old.digest(of) == self.new.digest(nf)

    def slots(self, ovt, nvt):
        """(old fn rva, new fn rva) per leading code-pointer slot, bounded by the next referenced data object."""
        ob, nb = self.base(self.old), self.base(self.new)
        i = bisect.bisect_right(self._dkeys, ovt)
        end = self._dkeys[i] - ovt if i < len(self._dkeys) else 0x800
        out = []
        for k in range(0, min(end, 0x800), 8):
            if ovt + k + 8 > len(self.old.img) or nvt + k + 8 > len(self.new.img): break
            a = struct.unpack_from("<Q", self.old.img, ovt + k)[0] - ob
            b = struct.unpack_from("<Q", self.new.img, nvt + k)[0] - nb
            if not (0 <= a < self.old.size and self.old.is_code(a) and 0 <= b < self.new.size and self.new.is_code(b)): break
            out.append((a, b))
        return out

    def local_best(self, f):
        """Best-similarity new function near f's predicted position (delta of the nearest anchors)."""
        i = bisect.bisect_left(self._anchor_starts, (f[0], -1 << 40))
        deltas = {d for _, d in self._anchor_starts[max(0, i - 2):i + 2]}
        ot = self.old.norm(f)[0]; best = (0.0, None)
        for d in deltas:
            for nf in self.new.funcs[bisect.bisect_left(self.new.starts, f[0] + d - 0x200):]:
                if nf[0] > f[0] + d + 0x200: break
                ratio = difflib.SequenceMatcher(None, ot, self.new.norm(nf)[0], autojunk=False).ratio()
                if ratio > best[0]: best = (ratio, nf)
        return best

    _bases = {}
    def base(self, im):
        if im.path not in self._bases:
            # the loader writes the actual (ASLR) base into the mapped header's ImageBase
            e = struct.unpack_from("<I", im.img, 0x3c)[0]
            self._bases[im.path] = struct.unpack_from("<Q", im.img, e + 0x18 + 0x18)[0]
        return self._bases[im.path]

    def alignment(self, of, nf):
        ot, oo, orf = self.old.norm(of); nt, no, nrf = self.new.norm(nf)
        if ot == nt: return [(i, i) for i in range(len(ot))]
        sm = difflib.SequenceMatcher(None, ot, nt, autojunk=False)
        pairs = []
        for a, b, n in sm.get_matching_blocks():
            pairs += [(a + k, b + k) for k in range(n)]
        return pairs

    def aligned_refs(self, of, nf):
        _, _, orf = self.old.norm(of); _, _, nrf = self.new.norm(nf)
        for i, j in self.alignment(of, nf):
            if len(orf[i]) == len(nrf[j]):
                yield from zip(orf[i], nrf[j])

    def map(self, rva):
        """-> (new rva or None, status text)"""
        old, new = self.old, self.new
        f = old.func_at(rva)
        if not f and old.is_code(rva):
            # a leaf without unwind data (thunks): the same bytes, found once in the new code
            pat = old.img[rva:rva + 24]
            hits = [m.start() for m in re.finditer(re.escape(pat), new.img) if new.is_code(m.start())]
            if len(hits) == 1: return hits[0], "leaf, unique 24-byte match"
            # several copies: the one at the shift of the nearest anchored functions
            i = bisect.bisect_left(self._anchor_starts, (rva, -1 << 40))
            near = [h for h in hits if any(abs(h - (rva + d)) < 0x40 for _, d in self._anchor_starts[max(0, i - 2):i + 2])]
            if len(near) == 1: return near[0], f"leaf, 24-byte match at the neighbours' shift ({len(hits)} copies)"
            return None, f"leaf UNMAPPED ({len(hits)} byte matches)"
        if f and old.is_code(rva):
            nf = self.fmap.get(f)
            if not nf or not self.same(f, nf):
                ratio, lb = self.local_best(f)
                cur = difflib.SequenceMatcher(None, old.norm(f)[0], new.norm(nf)[0], autojunk=False).ratio() if nf else 0.0
                if lb and ratio > cur: nf = lb
                if not nf or max(ratio, cur) < 0.6: return None, f"function {f[0]:#x} UNMATCHED (best {max(ratio, cur):.2f})"
            tag = "identical" if self.same(f, nf) else \
                f"CHANGED ({difflib.SequenceMatcher(None, old.norm(f)[0], new.norm(nf)[0], autojunk=False).ratio():.3f} similar)"
            _, oo, _ = old.norm(f); _, no, _ = new.norm(nf)
            off = rva - f[0]
            if off in oo:
                i = oo.index(off)
                for a, b in self.alignment(f, nf):
                    if a == i: return nf[0] + no[b], f"in {f[0]:#x}->{nf[0]:#x} {tag}"
                return None, f"in {f[0]:#x}->{nf[0]:#x} {tag}, instruction not aligned"
            return None, f"in {f[0]:#x}->{nf[0]:#x} {tag}, not an instruction boundary"
        c = self.data_votes.get(rva)
        if c:
            (nt, n), *rest = c.most_common()
            amb = f", also {[(hex(t), k) for t, k in rest[:3]]}" if rest else ""
            return nt, f"data, {n} votes{amb}"
        # inside a referenced data object: nearest mapped data start below, same delta
        keys = sorted(k for k in self.data_votes if k <= rva and rva - k < 0x400)
        if keys:
            k = keys[-1]; nt = self.data_votes[k].most_common(1)[0][0]
            return nt + (rva - k), f"data, {rva - k:#x} past mapped {k:#x}"
        return None, "data UNMAPPED"

    def vtable_check(self, ovt, nvt):
        """(agree, disagree, unknown) over the leading code-pointer slots: each old slot's function mapped vs new."""
        agree = dis = unk = 0
        self.first_bad = None
        for k, (a, b) in zip(range(0, 0x800, 8), self.slots(ovt, nvt)):
            fa = self.old.func_at(a)
            m = self.fmap.get(fa) if fa and fa[0] == a else None
            if m is None:
                # COMDAT-folded stubs and leaves: compare the bytes
                if self.old.img[a:a + 16] == self.new.img[b:b + 16]: agree += 1
                else: unk += 1
            elif m[0] == b: agree += 1
            else:
                dis += 1
                if self.first_bad is None: self.first_bad = k
        return agree, dis, unk

    def insn(self, im, rva):
        for ins in im.md.disasm(im.img[rva:rva + 16], rva, 1):
            return f"{ins.mnemonic} {ins.op_str}"
        return "?"


def default_old():
    arch = os.path.join(os.path.dirname(ROOT), "grim-dawn-archive")
    ds = sorted(os.listdir(arch), key=lambda d: os.path.getmtime(os.path.join(arch, d)))
    return os.path.join(arch, ds[-1], "GrimDawn.unpacked.bin")


def scan(files):
    """(rva, kind, where) for RVA constants and exe+0x evidence mentions."""
    out = []
    for fn in files:
        for ln, line in enumerate(open(fn, encoding="utf-8"), 1):
            where = f"{os.path.basename(fn)}:{ln}"
            for m in re.finditer(r"exe\+(0x[0-9a-f]+)", line): out.append((int(m.group(1), 16), "evidence", where))
            code = line.split("//")[0]
            if re.search(r"constexpr uintptr_t", code) or re.match(r"\s*\{0x[0-9a-f]+, \"", code):
                for m in re.finditer(r"\b(0x[0-9a-f]{5,6})\b", code): out.append((int(m.group(1), 16), "constant", where))
            for m in re.finditer(r"!= (0x[0-9a-f]{5,6})\)", code): out.append((int(m.group(1), 16), "constant", where))
    return out


def main():
    a = sys.argv[1:]
    if len(a) < 3: sys.exit(__doc__)
    op = default_old() if a[0] == "-" else a[0]
    np_ = os.path.join(ROOT, "build", "GrimDawn.unpacked.bin") if a[1] == "-" else a[1]
    old, new = Image(op), Image(np_)
    if a[2] == "xdiff":
        # instruction diff of the exports whose decorated name matches
        rx = re.compile(a[3])
        for nm in sorted(n for n in old.exports if rx.search(n) and n in new.exports):
            f, nf = old.func_at(old.exports[nm]), new.func_at(new.exports[nm])
            if not f or not nf: continue
            ot, oo, _ = old.norm(f); nt, no, _ = new.norm(nf)
            if ot == nt: print(f"--- same {nm}"); continue
            print(f"--- {nm}  {f[0]:#x} -> {nf[0]:#x}")
            for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, ot, nt, autojunk=False).get_opcodes():
                if tag == "equal": continue
                for i in range(i1, i2): print(f"  - {f[0] + oo[i]:#x}  {ot[i]}")
                for j in range(j1, j2): print(f"  + {nf[0] + no[j]:#x}  {nt[j]}")
        return
    if a[2] == "vtcmp":
        # exported vftables (??_7...) slot by slot: the same export name at each slot, or (unnamed) the same body
        rx = re.compile(a[3]); names = []
        for im in (old, new):
            rev = defaultdict(list)
            for n, v in im.exports.items(): rev[v].append(n)
            names.append(rev)
        b0, b1 = Reloc.base(Reloc, old), Reloc.base(Reloc, new)
        vts = [sorted(v for n, v in im.exports.items() if n.startswith("??_7")) for im in (old, new)]
        def body(im, p):
            # normalized body: the pdata function, else the leaf's first 8 instructions (thunks have no unwind data)
            f = im.func_at(p)
            if f and f[0] == p: return im.digest(f)
            toks = []
            for ins in im.md.disasm(im.img[p:p + 64], p, 8):
                toks.append(ins.mnemonic + " " + re.sub(r"0x[0-9a-f]{5,}", "X", ins.op_str))
                if ins.mnemonic in ("ret", "jmp"): break
            return "\n".join(toks)
        for nm in sorted(n for n in old.exports if n.startswith("??_7") and rx.search(n) and n in new.exports):
            o, n = old.exports[nm], new.exports[nm]; k = 0; bad = []
            i = bisect.bisect_right(vts[0], o); oend = vts[0][i] - o if i < len(vts[0]) else 0x1000
            i = bisect.bisect_right(vts[1], n); nend = vts[1][i] - n if i < len(vts[1]) else 0x1000
            while k < min(oend, 0x1000):
                p = struct.unpack_from("<Q", old.img, o + k)[0] - b0; q = struct.unpack_from("<Q", new.img, n + k)[0] - b1
                if not (0 <= p < old.size and old.is_code(p)): break
                if not (0 <= q < new.size and new.is_code(q)): bad.append(f"+{k:#x} ends early"); break
                on, nn = sorted(names[0].get(p, [])), sorted(names[1].get(q, []))
                # COMDAT folding hands one body several names: equal names OR an equal body is the same slot
                same = bool(set(on) & set(nn)) or body(old, p) == body(new, q)
                if not same: bad.append(f"+{k:#x} {on[:1] or hex(p)} -> {nn[:1] or hex(q)}")
                k += 8
            nk = 0
            while nk < min(nend, 0x1000) and 0 <= struct.unpack_from("<Q", new.img, n + nk)[0] - b1 < new.size \
                    and new.is_code(struct.unpack_from("<Q", new.img, n + nk)[0] - b1): nk += 8
            print(f"{'same' if not bad and nk == k else 'DIFF'}  {nm}  slots {k // 8} -> {nk // 8}")
            for x in bad[:6]: print("    ", x)
        return
    if a[2] == "exports":
        # same-named exports compared by normalized body: an unchanged accessor / ctor keeps its offsets
        rx = re.compile(a[3]) if len(a) > 3 else re.compile("")
        for nm in sorted(n for n in old.exports if rx.search(n)):
            if nm not in new.exports: print(f"GONE     {nm}"); continue
            f, nf = old.func_at(old.exports[nm]), new.func_at(new.exports[nm])
            if not f or not nf: continue
            ot, nt = old.norm(f)[0], new.norm(nf)[0]
            if ot == nt: print(f"same     {nm}") if "-v" in a else None
            else: print(f"CHANGED  {difflib.SequenceMatcher(None, ot, nt, autojunk=False).ratio():.3f}  {nm}")
        return
    print(f"old {op} ts={old.ts:#x} funcs={len(old.funcs)}\nnew {np_} ts={new.ts:#x} funcs={len(new.funcs)}")
    r = Reloc(old, new)
    print(f"matched {len(r.fmap)} functions ({len(r.identical)} by unique identical body), {len(r.data_votes)} data refs")
    if a[2] == "diff":
        for x in a[3:]:
            f = old.func_at(int(x, 16)); nr, st = r.map(f[0]); nf = new.func_at(nr)
            print(f"--- {f[0]:#x} -> {nf[0]:#x}: {st}")
            ot, oo, _ = old.norm(f); nt, no, _ = new.norm(nf)
            sm = difflib.SequenceMatcher(None, ot, nt, autojunk=False)
            for tag, i1, i2, j1, j2 in sm.get_opcodes():
                if tag == "equal": continue
                for i in range(i1, i2): print(f"  - {f[0] + oo[i]:#x}  {ot[i]}")
                for j in range(j1, j2): print(f"  + {nf[0] + no[j]:#x}  {nt[j]}")
        return
    if a[2] == "--apply":
        # Rewrite the RVA constants and the exe+0x evidence of FILEs to the new build in one pass (no chained
        # substitution), refuse when anything is unmapped, and re-check every byte signature at its new RVA.
        items = scan(a[3:]); m = {}; bad = []
        for rva, kind, where in items:
            nr, st = r.map(rva)
            if nr is None: bad.append(f"{rva:#x} [{kind} {where}] {st}")
            else: m[rva] = nr
        if bad and "--force" not in a: sys.exit("unmapped, nothing written (--force writes the rest):\n  " + "\n  ".join(bad))
        def sub_hex(mo):
            v = int(mo.group(2), 16)
            return mo.group(1) + (f"{m[v]:#x}" if v in m else mo.group(2))
        for fn in [x for x in a[3:] if x != "--force"]:
            src = open(fn, encoding="utf-8").read().split("\n"); n = 0
            for i, line in enumerate(src):
                code, sep, cmt = line.partition("//")
                if re.search(r"constexpr uintptr_t", code) or re.match(r"\s*\{0x[0-9a-f]+, \"", code) or re.search(r"!= 0x[0-9a-f]{5,6}\)", code):
                    code = re.sub(r"(\b)(0x[0-9a-f]{5,6})\b", sub_hex, code)
                cmt = re.sub(r"(exe\+)(0x[0-9a-f]+)", sub_hex, cmt)
                code = re.sub(r"(exe\+)(0x[0-9a-f]+)", sub_hex, code)
                out = code + sep + cmt
                if out != line: src[i] = out; n += 1
                mo = re.match(r'\s*\{(0x[0-9a-f]+), "([^"]+)", "((?:\\x[0-9a-f]{2})+)"\}', out)
                if mo:
                    sig = bytes(int(h, 16) for h in re.findall(r"\\x([0-9a-f]{2})", mo.group(3)))
                    nr = int(mo.group(1), 16)
                    if new.img[nr:nr + len(sig)] != sig: print(f"SIGNATURE DIFFERS {fn}:{i + 1} {mo.group(2)} at {nr:#x}: new bytes {new.img[nr:nr + len(sig)].hex()}")
            open(fn, "w", encoding="utf-8", newline="").write("\n".join(src))
            print(f"{fn}: {n} lines rewritten")
        return
    items = scan(a[3:]) if a[2] == "--scan" else [(int(x, 16), "rva", "") for x in a[2:]]
    seen = set()
    for rva, kind, where in items:
        if (rva, kind) in seen: continue
        seen.add((rva, kind))
        nr, st = r.map(rva)
        line = f"{rva:#08x} -> {nr:#08x}" if nr is not None else f"{rva:#08x} -> ????????"
        line += f"  [{kind} {where}] {st}"
        if nr is not None and not old.is_code(rva):
            ag, dis, unk = r.vtable_check(rva, nr)
            if ag + dis + unk:
                line += f"  vtable slots: {ag} agree, {dis} disagree, {unk} unknown"
                if dis: line += f", first disagree at +{r.first_bad:#x}   <-- CHECK"
        if kind == "evidence" and nr is not None and old.is_code(rva):
            oi, ni = r.insn(old, rva), r.insn(new, nr)
            rip = lambda s: re.sub(r"\[rip [+-] 0x[0-9a-f]+\]", "[RIP]", s)
            if rip(oi) != rip(ni): line += f"\n      old: {oi}\n      new: {ni}   <-- DIFFERS"
        print(line)


if __name__ == "__main__":
    main()
