"""Read a character's fog of war (map.fow) -- the explored mask of EVERY region, loaded or not.
Usage: uv run tools/fow.py [character] [difficulty=Normal] [--region <substring>] [--grid] [--summary]

Where: <Steam>/userdata/<id>/219990/remote/save/main/_<character>/levels_world001.map/<Difficulty>/map.fow
(Steam cloud saves; the path is found via the HKCU Steam path). The live game keeps the same data in
World::GetFOWManager() (a map name -> blob) and loads a region's grid from it lazily in Region::GetFogOfWar(false);
it writes the file on save. Format (Engine FOWManager::Read 0x8cb10 / FogOfWar::Read 0x8c6a0, read 2026-09-28):
  file   = u32 uncompressed size, then one LZ4 block
  block  = "FOWX", u32 version (1), u32 count, count x { u32 name length, name (e.g. "Levels/Region0A001.lvl"),
           u32 blob size, blob }
  blob   = u32 width, u32 height, i32 n (= width * height), u32 mode; mode 0 and size == n + 16 -> n raw bytes
           follow (row 0 = the region's far-z edge), mode 2 -> all seen (0), anything else -> all fogged (0xff)
  cell   = 8 x 8 world units, a byte > 150 is fogged (FogOfWar::IsInFog); a 128-u region is 18 x 18 (16 + a ring).
"""
import argparse, os, struct, sys, winreg


def steam_path():
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as k:
        return winreg.QueryValueEx(k, "SteamPath")[0]


def find_fow(character, difficulty):
    root = os.path.join(steam_path(), "userdata")
    for uid in os.listdir(root):
        p = os.path.join(root, uid, "219990", "remote", "save", "main", "_" + character, "levels_world001.map", difficulty, "map.fow")
        if os.path.exists(p):
            return p
    sys.exit(f"no map.fow for character {character!r} ({difficulty}) under {root}")


def lz4_fast(src, size):
    """LZ4 block decode that stops at `size` output bytes, like the game's LZ4_decompress_fast. The python lz4
    package's safe decoder rejects these files: their last sequence reads 4 bytes past the end of the file (the game's
    decoder reads whatever follows the buffer), so the input is padded and the last few output bytes -- the tail of the
    last region's grid -- are 0xff."""
    src = src + b"\xff" * 64
    out, i = bytearray(), 0
    while len(out) < size:
        tok = src[i]; i += 1
        lit = tok >> 4
        if lit == 15:
            while True:
                b = src[i]; i += 1; lit += b
                if b != 255: break
        out += src[i:i + lit]; i += lit
        if len(out) >= size: break
        off = src[i] | (src[i + 1] << 8); i += 2
        ml = tok & 15
        if ml == 15:
            while True:
                b = src[i]; i += 1; ml += b
                if b != 255: break
        ml += 4
        start = len(out) - off
        if off == 0 or start < 0: break   # past the real input
        for k in range(ml):   # may overlap its own output
            out.append(out[start + k])
    return bytes(out[:size]).ljust(size, b"\xff")


def read_fow(path):
    raw = open(path, "rb").read()
    size = struct.unpack_from("<I", raw, 0)[0]
    d = lz4_fast(raw[4:], size)
    if d[:4] != b"FOWX" or struct.unpack_from("<I", d, 4)[0] != 1:
        sys.exit("not a FOWX v1 block")
    count = struct.unpack_from("<I", d, 8)[0]
    o, regions = 12, {}
    for _ in range(count):
        nl = struct.unpack_from("<I", d, o)[0]; o += 4
        name = d[o:o + nl].decode("latin-1"); o += nl
        bl = struct.unpack_from("<I", d, o)[0]; o += 4
        blob = d[o:o + bl]; o += bl
        w, h, n, mode = struct.unpack_from("<IIiI", blob, 0)
        if mode == 0 and bl == n + 16:
            cells = blob[16:16 + n]
        else:
            cells = bytes([0 if mode == 2 else 0xFF]) * (w * h)
        regions[name] = (w, h, cells)
    return regions


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("character", nargs="?", default="claude")
    ap.add_argument("difficulty", nargs="?", default="Normal")
    ap.add_argument("--region")
    ap.add_argument("--grid", action="store_true")
    a = ap.parse_args()
    path = find_fow(a.character, a.difficulty)
    regions = read_fow(path)
    seen_total = sum(1 for w, h, c in regions.values() for r in range(1, h - 1) for x in range(1, w - 1) if c[r * w + x] <= 150)
    print(f"{path}\n{len(regions)} regions, {seen_total} seen inner cells")
    for name, (w, h, cells) in sorted(regions.items()):
        if a.region and a.region.lower() not in name.lower():
            continue
        inner = [cells[r * w + x] for r in range(1, h - 1) for x in range(1, w - 1)]
        seen = sum(1 for b in inner if b <= 150)
        print(f"  {name} {w}x{h}: {seen} of {len(inner)} inner cells seen")
        if a.grid:
            for r in range(h):
                print("    " + "".join("#" if cells[r * w + x] > 150 else "." for x in range(w)))


if __name__ == "__main__":
    main()
