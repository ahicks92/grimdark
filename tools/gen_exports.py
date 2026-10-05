"""Dump exports of Grim Dawn x64 DLLs: .def files (for import libs) + undecorated listings.
Usage: uv run --with pefile tools/gen_exports.py [gamedir]
"""
import ctypes, os, sys, pefile
from gamepath import GAME_DIR
GAME = sys.argv[1] if len(sys.argv) > 1 else GAME_DIR
OUT = os.path.join(os.path.dirname(__file__), "exports")
dbghelp = ctypes.windll.dbghelp
UNDNAME_COMPLETE = 0
def undecorate(name: str) -> str:
    buf = ctypes.create_string_buffer(4096)
    n = dbghelp.UnDecorateSymbolName(name.encode(), buf, 4096, UNDNAME_COMPLETE)
    return buf.value.decode(errors="replace") if n else name
for dll in ["Engine.dll", "Game.dll", "DirectInput.dll"]:
    pe = pefile.PE(os.path.join(GAME, "x64", dll), fast_load=True, max_symbol_exports=200000)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]])
    syms = sorted(s.name.decode() for s in pe.DIRECTORY_ENTRY_EXPORT.symbols if s.name)
    stem = dll[:-4]
    with open(os.path.join(OUT, f"{stem}.x64.def"), "w") as f:
        f.write(f"LIBRARY {stem}\nEXPORTS\n")
        for s in syms: f.write(f"    {s}\n")
    with open(os.path.join(OUT, f"{stem}.x64.txt"), "w", encoding="utf-8") as f:
        for s in syms: f.write(f"{undecorate(s)}\t{s}\n")
    print(f"{dll}: {len(syms)} exports")
