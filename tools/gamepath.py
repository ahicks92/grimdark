"""The Grim Dawn install folder, for the dev tools: GRIMDARK_GAME_DIR, else Steam's own lookup (HKCU SteamPath ->
steamapps/libraryfolders.vdf -> the library holding appmanifest_219990.acf -> its installdir, as gdlaunch does),
else the default Steam folder. Stdlib only."""
import os, re

DEFAULT = r"C:\Program Files (x86)\Steam\steamapps\common\Grim Dawn"


def _steam_lookup():
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as k:
            steam = winreg.QueryValueEx(k, "SteamPath")[0]
    except OSError:
        return None
    libs = [steam]
    try:
        with open(os.path.join(steam, "steamapps", "libraryfolders.vdf"), encoding="utf-8", errors="replace") as f:
            libs += [p.replace("\\\\", "\\") for p in re.findall(r'"path"\s+"([^"]*)"', f.read())]
    except OSError:
        pass
    for lib in libs:
        try:
            with open(os.path.join(lib, "steamapps", "appmanifest_219990.acf"), encoding="utf-8", errors="replace") as f:
                m = re.search(r'"installdir"\s+"([^"]*)"', f.read())
        except OSError:
            continue
        if m:
            d = os.path.join(lib, "steamapps", "common", m.group(1))
            if os.path.isdir(d):
                return os.path.normpath(d)
    return None


GAME_DIR = os.environ.get("GRIMDARK_GAME_DIR") or _steam_lookup() or DEFAULT
X64 = os.path.join(GAME_DIR, "x64")

if __name__ == "__main__":
    print(GAME_DIR)
