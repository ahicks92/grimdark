"""Which of a monster's skills the telegraph cues cover (offline, database.arz + src/telegraph.cpp's table).

Follows a monster's death-spawn pool (poolToSpawnOnDeath -> the phase-2 monster) and lists every skill each phase can
use (skillNameN, specialAttack*SkillName, attackSkillName, initialSkillName, chainInitialSkill / chainNextSkill), with
the skill's Class and the telegraph verdict: the shape it plays, "silent" (in the table as deliberately silent), or
UNKNOWN (not in the table: silent and counted in /telegraph). Passives are listed as silent by class.
Usage: uv run tools/telegraph_audit.py <monster-record-regex> [...]
  e.g. uv run tools/telegraph_audit.py "boss&quest/aetherialobelisk_00\\.dbr$" "dermapteran_madqueen\\.dbr$"
"""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import arz

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKILL_FIELDS = re.compile(r"^(skillName\d+|specialAttack\d*SkillName|attackSkillName|initialSkillName|chainInitialSkill|chainNextSkill)$")


def telegraph_table():
    src = open(os.path.join(ROOT, "src", "telegraph.cpp"), encoding="utf-8").read()
    body = src[src.index("const char* shape_of("):]
    body = body[:body.index("};")]
    return dict(re.findall(r'\{"([^"]+)",\s*"([^"]*)"\}', body))


def main():
    if len(sys.argv) < 2:
        print(__doc__); return
    d, strings, recs = arz.load()
    by_path = {r[0].lower(): r for r in recs}
    cache = {}
    def rec(path):
        p = path.lower()
        if p not in cache:
            r = by_path.get(p)
            cache[p] = arz.decode(d, strings, r[2], r[3], r[4]) if r else None
        return cache[p]
    table = telegraph_table()
    rxs = [re.compile(a, re.I) for a in sys.argv[1:]]
    roots = [r[0] for r in recs if any(x.search(r[0]) for x in rxs)]
    seen = set()
    queue = [(p, 1) for p in roots]
    while queue:
        path, phase = queue.pop(0)
        if path.lower() in seen: continue
        seen.add(path.lower())
        m = rec(path)
        if m is None: print(f"{path}: not found"); continue
        print(f"=== phase {phase}: {path}  ({(m.get('FileDescription') or [''])[0]})")
        skills = {}
        for k, v in m.items():
            if SKILL_FIELDS.match(k) and v and isinstance(v[0], str) and v[0]:
                skills.setdefault(v[0], []).append(k)
        for sp, fields in sorted(skills.items()):
            s = rec(sp)
            cls = (s.get("Class") or ["?"])[0] if s else "(missing record)"
            verdict = table.get(cls)
            verdict = "UNKNOWN" if verdict is None else (verdict or "silent")
            print(f"  {verdict:8} {cls:42} {sp.split('/')[-1]}  [{', '.join(sorted(fields))}]")
        pool = (m.get("poolToSpawnOnDeath") or [""])[0]
        if pool:
            pr = rec(pool) or {}
            for k, v in pr.items():
                if re.match(r"^name\d+$", k) and v and v[0]:
                    queue.append((v[0], phase + 1))


if __name__ == "__main__":
    main()
