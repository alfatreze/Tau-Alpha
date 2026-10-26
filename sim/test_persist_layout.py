#!/usr/bin/env python3
"""tools/persist_layout.py (the Python reference for Tau Omega) against the firmware: over random register files its table, unpack, pack, legacy detection, legacy unpack, migration and resumed migration equal the C code of
fw2/cores/settings_core.h (through sim/p2_settings_harness.c); its id -> word map equals the addresses of dist/.../interact.json and of the released v0.6.1; the committed golden vectors equal a fresh run; the real captures of the card
(docs/research/persist_layout/captures: TAU DEV 181 = legacy layout, DEV 182..184 = new) decode to the right layout. A mutant of the reference (a wrong shift, a missing converted mark) must be caught."""
import json, random, subprocess, sys, tempfile, zipfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import persist_layout as pl
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ((" " + str(extra)[:300]) if (extra and not ok) else ""))
    if not ok: fails += 1
def build(d):
    exe = d / "h"
    r = subprocess.run(["cc", "-O1", "-Wall", "-Werror", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_settings_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, text): return subprocess.run([str(exe)], input=text, capture_output=True, text=True).stdout.strip().split("\n")
def words(r):
    w = [0] * 32
    for i in (0, 1, 2, 5, 6, 8, 9, 10, 11, 15, 16, 17, 18, 19, 20, 21): w[i] = r.choice([0, 1, 2, 94, 100, 101, r.getrandbits(32), r.getrandbits(31), r.getrandbits(8), 1 << 27, 1 << 30])
    return w
def agree(ref):
    exe = ref["exe"]; r = random.Random(7); cases = [words(r) for _ in range(500)]; bad = []
    table = [tuple(map(int, l.split())) for l in run(exe, "T\n")]
    if table != [f[1:] for f in ref["fields"]]: bad.append("table")
    if list(map(int, run(exe, "O\n")[0].split())) != list(ref["order"]): bad.append("write order")
    txt = lambda c, w: "%s %s\n" % (c, " ".join(map(str, w)))
    got_u = run(exe, "".join(txt("u", w) for w in cases)); got_g = run(exe, "".join(txt("g", w) for w in cases)); got_l = run(exe, "".join(txt("l", w) for w in cases))
    got_m = run(exe, "".join(txt("m", w) for w in cases)); got_r = run(exe, "".join(txt("r", w) for w in cases))
    for i, w in enumerate(cases):
        if list(map(int, got_u[i].split())) != [ref["unpack"](w)[n] for n in pl.NAMES]: bad.append(("unpack", i))
        if int(got_g[i]) != int(ref["is_legacy"](w)): bad.append(("is_legacy", i))
        if list(map(int, got_l[i].split())) != [ref["legacy_unpack"](w)[n] for n in pl.NAMES]: bad.append(("legacy_unpack", i))
        if list(map(int, got_m[i].split())) != ref["migrate"](w): bad.append(("migrate", i))
        if list(map(int, got_r[i].split())) != ref["migrate_resume"](w): bad.append(("migrate_resume", i))
        vals = {n: r.randint(0, 40) for n in pl.NAMES}
        pk = run(exe, "p %s %s\n" % (" ".join(map(str, w)), " ".join(str(vals[n]) for n in pl.NAMES)))[0].split()
        if list(map(int, pk)) != ref["pack"](w, vals): bad.append(("pack", i))
    return bad
def reference(mutant=None):
    if mutant == "shift": fields = [(n, w, s + (1 if n == "COLOUR" else 0), wd, lo, hi, df) for n, w, s, wd, lo, hi, df in pl.FIELDS]
    else: fields = pl.FIELDS
    class M: pass
    # run the module's functions over (possibly mutated) fields
    def unpack(words):
        out = {}
        for name, word, sh, wd, lo, hi, df in fields:
            v = (words[word] >> sh) & pl._mask(wd); out[name] = v if lo <= v <= hi else df
        return out
    def pack(words, values):
        w = list(words)
        for name, word, sh, wd, lo, hi, df in fields:
            v = max(lo, min(hi, values.get(name, df))); m = pl._mask(wd) << sh
            w[word] = (w[word] & ~m & 0xFFFFFFFF) | ((v << sh) & m)
        for k in pl.MARKED_WORDS:
            if mutant != "mark": w[k] |= pl.NEW_MARK
        return w
    def migrate(words): return pack([0] * 32, pl.legacy_unpack(words))
    def migrate_resume(words):
        out = migrate(words)
        for k in pl.MARKED_WORDS:
            if words[k] & pl.NEW_MARK: out[k] = words[k]
        return out
    return {"fields": fields, "order": pl.WRITE_ORDER, "unpack": unpack, "pack": pack, "is_legacy": pl.is_legacy, "legacy_unpack": pl.legacy_unpack, "migrate": migrate, "migrate_resume": migrate_resume}
with tempfile.TemporaryDirectory() as td:
    exe = build(Path(td))
    ref = reference(); ref["exe"] = exe
    bad = agree(ref); check("the Python reference equals the firmware's C code: table, write order, unpack, pack, legacy detection and unpack, migration, resumed migration (500 random register files)", not bad, bad[:3])
    for m in ("shift", "mark"):
        r2 = reference(m); r2["exe"] = exe; check("mutant of the reference (%s) is caught" % ("a wrong shift" if m == "shift" else "no converted mark"), bool(agree(r2)))
ia = {int(v["id"]): (int(v["address"], 16) - 0x20000000) // 4 for v in json.loads((ROOT / "dist/Cores/alfatreze.TAU/interact.json").read_text())["interact"]["variables"] if v.get("persist")}
check("id -> word equals the addresses of dist/Cores/alfatreze.TAU/interact.json", all(pl.ID_TO_WORD[i] == w for i, w in ia.items()) and set(ia) <= set(pl.ID_TO_WORD), str(ia))
z = ROOT / "release/alfatreze.TAU_0.6.1_2026-10-21.zip"
if z.exists():
    old = {int(v["id"]): (int(v["address"], 16) - 0x20000000) // 4 for v in json.loads(zipfile.ZipFile(z).read("Cores/alfatreze.TAU/interact.json"))["interact"]["variables"] if v.get("persist")}
    check("id -> word equals the addresses of the released v0.6.1 (ids 24-26 included)", all(pl.ID_TO_WORD[i] == w for i, w in old.items()), str(old))
g = pl.golden()
check("the committed golden vectors equal a fresh run", json.loads((ROOT / "docs/research/persist_layout/golden_vectors.json").read_text()) == json.loads(json.dumps(g)))
check("the golden resumed migrations all end at the full migration", all(c["equals_full_migration"] for c in g["resume"]) and len(g["resume"]) >= 30)
cap = lambda n: json.loads((ROOT / f"docs/research/persist_layout/captures/dev{n}_interact_persist.json").read_text())
d181 = pl.describe(cap(181)); d184 = pl.describe(cap(184))
check("the real capture of TAU DEV 181 decodes as the legacy layout and migrates to a new-layout register file with the same settings", d181["layout"] == "legacy" and pl.unpack(pl.migrate(pl.words_from_persist(cap(181))))["VOLUME"] == d181["values"]["VOLUME"] == 73 and d181["values"]["COLOUR"] == 7)
check("the real captures of TAU DEV 182, 183 and 184 decode as the new layout (version 1, 16 variables)", all(pl.describe(cap(n))["layout"] == "new" and len(cap(n)["interact_persist"]["variables"]) == 16 for n in (182, 183, 184)) and d184["values"]["LAYOUT"] == 1)
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
