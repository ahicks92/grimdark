"""Which rooms are unexplored: a character's fog of war (tools/fow.py) laid over the rooms label grids.
Usage: uv run tools/fog_rooms.py <area key, e.g. vanguard | all> [character=claude] [difficulty=Normal] [--db path]
       (all = one line per area: rooms, rooms fully seen / partly / never)

Each room's walkable 0.25-u cells are looked up in the fog of the engine chunk that contains them (chunk placement
from world001.map via tools/gdmap; fog cell of region-relative (x, z) = c = int(x/8 + 1), r = h - int(z/8 + 1) - 1,
> 150 = fogged). Prints each room's fogged fraction. Base layer only (overlays are a few percent of cells).
"""
import argparse, os, sqlite3, sys, zlib
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fow  # noqa: E402
from gdmap.mapfile import WorldMap  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def decode_rle(blob, w, h):
    a = np.frombuffer(blob, dtype=np.dtype([("v", "<i2"), ("n", "<u2")]))
    return np.repeat(a["v"], a["n"]).reshape(h, w)


def area_report(db, area, fog, wm, summary):
    import json
    x0, z0, w, h, cell, labels, keys = db.execute("select x0, z0, w, h, cell, labels, label_keys from grids where region_key=?", (area,)).fetchone()
    try:
        labels = zlib.decompress(labels)
    except zlib.error:
        pass
    grid = decode_rle(labels, w, h)
    keys = json.loads(keys)
    titles = dict(db.execute("select key, coalesce(title, '') from rooms where region_key=?", (area,)).fetchall())
    chunks = json.loads(db.execute("select chunks from regions where key=?", (area,)).fetchone()[0])
    fogged = np.zeros((h, w), dtype=bool)
    cz, cx = np.mgrid[0:h, 0:w]
    wx = x0 + (cx + 0.5) * cell
    wz = z0 + (cz + 0.5) * cell
    for lvl in chunks:
        reg = wm.by_name.get(os.path.basename(lvl).removesuffix(".lvl").removeprefix("Region"))
        if reg is None:
            continue
        ox, _, oz = reg.world_offset
        fw, fh, fcells = fog.get(lvl, (18, 18, b"\xff" * 324))   # no entry = never visited = all fogged
        f = np.frombuffer(fcells, dtype=np.uint8).reshape(fh, fw)
        rx, rz = wx - ox, wz - oz
        inside = (rx >= 0) & (rx < (fw - 2) * 8) & (rz >= 0) & (rz < (fh - 2) * 8)
        c = (rx / 8 + 1).astype(int).clip(0, fw - 1)
        r = (fh - (rz / 8 + 1).astype(int) - 1).clip(0, fh - 1)
        fogged |= inside & (f[r, c] > 150)
    counts = np.bincount(grid[grid >= 0].ravel(), minlength=len(keys))
    fcounts = np.bincount(grid[(grid >= 0) & fogged].ravel(), minlength=len(keys))
    seen = part = never = 0
    lines = []
    for lab, key in enumerate(keys):
        n, nf = int(counts[lab]), int(fcounts[lab])
        if not n:
            continue
        frac = nf / n
        seen += frac < 0.05; never += frac > 0.95; part += 0.05 <= frac <= 0.95
        lines.append(f"  {key:28} {titles.get(key, '')[:40]:40} {n * cell * cell:7.0f} u2  {100 * frac:5.1f}% fogged")
    if summary:
        print(f"{area:32} {len(lines):4} rooms: {seen:4} seen, {part:4} partly, {never:4} never")
    else:
        print(f"{area}: {len(chunks)} chunks, {len(lines)} rooms: {seen} seen, {part} partly, {never} never")
        print("\n".join(lines))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("area")
    ap.add_argument("character", nargs="?", default="claude")
    ap.add_argument("difficulty", nargs="?", default="Normal")
    ap.add_argument("--db", default=os.path.join(ROOT, "build", "ninja", "assets", "rooms.db"))
    a = ap.parse_args()
    db = sqlite3.connect(a.db)
    fog = fow.read_fow(fow.find_fow(a.character, a.difficulty))
    wm = WorldMap()
    areas = [r[0] for r in db.execute("select region_key from grids order by region_key")] if a.area == "all" else [a.area]
    for area in areas:
        area_report(db, area, fog, wm, a.area == "all")


if __name__ == "__main__":
    main()
