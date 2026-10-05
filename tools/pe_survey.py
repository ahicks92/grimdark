"""Survey Grim Dawn x64 binaries: interesting imports, exports, RTTI class names."""
import pefile, re, sys, os
from gamepath import X64
G = X64
INTERESTING = re.compile(r"RawInput|DirectInput|SetWindowsHookEx|SendInput|keybd_event|BlockInput|GetAsyncKeyState|GetKeyState|GetKeyboardState|MapVirtualKey|SystemParametersInfo|RegisterHotKey|SetCursor|ClipCursor|XInput|GetRawInputData|DefRawInputProc|ToUnicode|ActivateKeyboardLayout", re.I)
for name in ["Grim Dawn.exe", "Engine.dll", "DirectInput.dll", "Widget.dll", "Game.dll"]:
    path = os.path.join(G, name)
    pe = pefile.PE(path, fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"], pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]])
    print(f"\n##### {name}  machine=0x{pe.FILE_HEADER.Machine:x}")
    dlls = []
    hits = []
    for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
        d = entry.dll.decode(errors="replace")
        dlls.append(d)
        for imp in entry.imports:
            n = imp.name.decode(errors="replace") if imp.name else f"ord{imp.ordinal}"
            if INTERESTING.search(n):
                hits.append(f"{d}!{n}")
    print("imports from:", ", ".join(dlls))
    print("interesting imports:", ", ".join(hits) if hits else "(none)")
    exp = getattr(pe, "DIRECTORY_ENTRY_EXPORT", None)
    if exp:
        syms = [s.name.decode(errors="replace") for s in exp.symbols if s.name]
        print(f"exports: {len(syms)}; sample:", syms[:8])
    else:
        print("exports: none")
    # RTTI type descriptor names: ".?AV<Name>@<ns>@@"
    data = pe.__data__
    names = set(m.group(0).decode() for m in re.finditer(rb"\.\?A[VU][A-Za-z0-9_@<>, ]{3,200}?@@", data))
    print(f"RTTI type descriptors: {len(names)}")
    if name in ("Grim Dawn.exe", "Widget.dll"):
        ui = sorted(n for n in names if re.search(r"Menu|Screen|Window|Widget|Tooltip|Button|List|Hud|Panel|Dialog|Page|Tab|Slider|Check|Text|Label|Scroll|Combo|Edit|Popup|Bar|Box", n))
        print(f"UI-ish classes ({len(ui)}):")
        for n in ui: print("   ", n)
