"""Archive the current game build's binaries + the unpacked exe dump, so a later patch can be relocated
against them (the RVA/offset tables in src/exe_ui.cpp are measured against ONE build, and Steam overwrites
the old one on patch day). Run BEFORE letting Steam update.

  uv run tools/archive_build.py --version 1.3.0.8 [--dest ../grim-dawn-archive] [--force]

Copies x64\\Grim Dawn.exe (SteamStub-packed, as shipped), Engine.dll, Game.dll, DirectInput.dll and
build/GrimDawn.unpacked.bin (from tools/dump_exe.py, the in-memory unpacked image exe_dis.py reads) into
<dest>/<version>-<pe-timestamp>/ with a manifest.json of sizes + SHA-256s. Refuses when the dump is missing or
was taken from a different exe (its PE header timestamp must match the file on disk): an archive with a
stale dump is worse than none. The archive is Crate's code: keep it OUT of the repo.
"""
import argparse, datetime, hashlib, json, os, shutil, struct, sys
from gamepath import GAME_DIR

GAME = GAME_DIR
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DUMP = os.path.join(ROOT, "build", "GrimDawn.unpacked.bin")
FILES = ["Grim Dawn.exe", "Engine.dll", "Game.dll", "DirectInput.dll"]


def pe_timestamp(path):
    with open(path, "rb") as f:
        head = f.read(0x1000)
    e_lfanew = struct.unpack_from("<I", head, 0x3c)[0]
    if head[e_lfanew:e_lfanew + 4] != b"PE\0\0":
        sys.exit(f"{path}: not a PE image")
    return struct.unpack_from("<I", head, e_lfanew + 8)[0]


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", required=True, help="the game's own version string, e.g. 1.3.0.8 (the exe's version resource is meaningless)")
    ap.add_argument("--dest", default=os.path.join(ROOT, "..", "grim-dawn-archive"))
    ap.add_argument("--game", default=GAME)
    ap.add_argument("--force", action="store_true", help="overwrite an existing folder for this build")
    a = ap.parse_args()

    exe = os.path.join(a.game, "x64", "Grim Dawn.exe")
    if not os.path.exists(exe): sys.exit(f"game exe not found: {exe}")
    if not os.path.exists(DUMP): sys.exit(f"no unpacked dump at {DUMP}; run `uv run tools/dump_exe.py` against the running game first")
    ts_exe, ts_dump = pe_timestamp(exe), pe_timestamp(DUMP)
    if ts_exe != ts_dump:
        sys.exit(f"dump is stale: exe PE timestamp {ts_exe:#x}, dump {ts_dump:#x}; re-dump from the running game before archiving")

    folder = os.path.abspath(os.path.join(a.dest, f"{a.version}-{ts_exe:08x}"))
    if os.path.exists(folder) and not a.force: sys.exit(f"already archived: {folder} (pass --force to overwrite)")
    os.makedirs(folder, exist_ok=True)

    # The three timestamps are the row the mod's version gate needs (src/game_versions.h).
    ts_engine = pe_timestamp(os.path.join(a.game, "x64", "Engine.dll"))
    ts_game = pe_timestamp(os.path.join(a.game, "x64", "Game.dll"))
    print(f"game_versions.h row: {{\"{a.version} Steam\", {ts_exe:#x}, {ts_engine:#x}, {ts_game:#x}}}")
    manifest = {"version": a.version, "pe_timestamp": f"{ts_exe:#x}",
                "engine_pe_timestamp": f"{ts_engine:#x}", "game_pe_timestamp": f"{ts_game:#x}",
                "pe_timestamp_utc": datetime.datetime.fromtimestamp(ts_exe, datetime.UTC).isoformat(),
                "archived_utc": datetime.datetime.now(datetime.UTC).isoformat(), "source": a.game, "files": {}}
    for name in FILES + ["GrimDawn.unpacked.bin"]:
        src = DUMP if name == "GrimDawn.unpacked.bin" else os.path.join(a.game, "x64", name)
        dst = os.path.join(folder, name)
        shutil.copy2(src, dst)
        manifest["files"][name] = {"size": os.path.getsize(dst), "sha256": sha256(dst)}
        print(f"{name}: {manifest['files'][name]['size']} bytes")
    with open(os.path.join(folder, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
    print(f"archived {a.version} (pe {ts_exe:#x}) -> {folder}")


if __name__ == "__main__":
    main()
