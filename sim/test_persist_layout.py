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
    """a register file: a third random junk (any layout version), a third a layout-0 card, a third a layout-1 card with tunings (valid, damaged, of a shipped meter or not)"""
    c = r.random()
    if c < 0.34:
        w = [0] * 32
        for i in (0, 1, 2, 5, 6, 8, 9, 10, 11, 15, 16, 17, 18, 19, 20, 21): w[i] = r.choice([0, 1, 2, 94, 100, 101, r.getrandbits(32), r.getrandbits(31), r.getrandbits(8), 1 << 27, 2 << 27, 1 << 30, 1 << 29])
        return w
    if c < 0.67:
        w = [0] * 32
        w[0] = r.randint(0, 100); w[1] = r.randint(0, 18); w[2] = r.randint(0, 2); w[5] = r.randint(0, 14); w[6] = r.randint(0, 30); w[15] = r.randint(0, 15)
        w[16] = r.randint(0, 31); w[17] = r.randint(0, 1) | (r.randint(0, 2) << 4); w[18] = r.getrandbits(27); w[19] = r.getrandbits(31); w[20] = r.getrandbits(31)
        for k in (8, 9, 10, 11): w[k] = r.choice([0, r.getrandbits(31)])
        return w
    return pl.random_v1_words(r)
def words_v0(r):
    w = [0] * 32
    w[0] = r.randint(0, 100); w[1] = r.randint(0, 18); w[2] = r.randint(0, 2); w[5] = r.randint(0, 14); w[6] = r.randint(0, 30); w[15] = r.randint(0, 15)
    w[16] = r.randint(0, 31); w[17] = r.randint(0, 1) | (r.randint(0, 2) << 4); w[18] = r.getrandbits(27); w[19] = r.getrandbits(31); w[20] = r.getrandbits(31)
    for k in (8, 9, 10, 11): w[k] = r.choice([0, r.getrandbits(31)])
    return w
FIX = "".join("M %d %d %s\n" % (i, len(pl.fixture_max(i)), " ".join(map(str, pl.fixture_max(i)))) for i in range(len(pl.SHIPPED)))
def agree(ref):
    exe = ref["exe"]; r = random.Random(7); cases = [words(r) for _ in range(500)]; bad = []
    table = [tuple(map(int, l.split())) for l in run(exe, "T\n")]
    if table != [f[1:] for f in ref["fields"]]: bad.append("table")
    if list(map(int, run(exe, "O\n")[0].split())) != list(ref["order"]): bad.append("write order")
    if [tuple(map(int, l.split())) for l in run(exe, "S\n")] != [(i, h, rv) for i, h, rv in pl.SHIPPED]: bad.append("the shipped meter ids (fw2/cores/shipped_meter_ids.h)")
    txt = lambda c, w: "%s %s\n" % (c, " ".join(map(str, w)))
    halves = []
    for w in cases:                                                         # a migration interrupted after k writes of the write order (word 0 last, not yet written)
        full = ref["migrate"](w)
        if pl.needs_migration(w):
            k = r.randint(0, len(pl.WRITE_ORDER)); h = list(w)
            for word in pl.WRITE_ORDER[:k]: h[word] = full[word]
            halves.append(h)
        else: halves.append(w)
    got_u = run(exe, "".join(txt("u", w) for w in cases)); got_g = run(exe, "".join(txt("g", w) for w in cases)); got_l = run(exe, "".join(txt("l", w) for w in cases))
    got_m = run(exe, FIX + "".join(txt("m", w) for w in cases)); got_r = run(exe, FIX + "".join(txt("r", h) for h in halves)); got_m0 = run(exe, "".join(txt("m0", w) for w in cases))
    for i, w in enumerate(cases):
        if list(map(int, got_u[i].split())) != [ref["unpack"](w)[n] for n in pl.NAMES]: bad.append(("unpack", i))
        if int(got_g[i]) != int(ref["is_legacy"](w)): bad.append(("is_legacy", i))
        if list(map(int, got_l[i].split())) != [ref["legacy_unpack"](w)[n] for n in pl.NAMES]: bad.append(("legacy_unpack", i))
        if list(map(int, got_m[i].split())) != ref["migrate"](w): bad.append(("migrate", i))
        if list(map(int, got_m0[i].split())) != ref["migrate"](w, None): bad.append(("migrate without tuning table", i))
        if list(map(int, got_r[i].split())) != ref["migrate_resume"](halves[i]): bad.append(("migrate_resume", i))
        vals = {n: r.randint(0, 40) for n in pl.NAMES}
        pk = run(exe, "p %s %s\n" % (" ".join(map(str, w)), " ".join(str(vals[n]) for n in pl.NAMES)))[0].split()
        if list(map(int, pk)) != ref["pack"](w, vals): bad.append(("pack", i))
    return bad
class Mutant:
    """a mutation of the reference (monkeypatched into tools/persist_layout.py for the duration): a wrong shift, no converted mark, the wrong tuning key, the migration order with word 5 before word 6"""
    def __init__(self, kind): self.kind = kind
    def __enter__(self):
        self.save = (pl.FIELDS, pl.MARKS, pl.key_of_index, pl.WRITE_ORDER, pl.hash31_of_index)
        if self.kind == "shift": pl.FIELDS = [(n, w, s + (1 if n == "COLOUR" else 0), wd, lo, hi, df) for n, w, s, wd, lo, hi, df in pl.FIELDS]
        if self.kind == "mark": pl.MARKS = ((1, 30), (2, 30))
        if self.kind == "key": pl.key_of_index = lambda i, f=pl.key_of_index: None if f(i) is None else (f(i) & 0xFFFF)          # forgets params_rev
        if self.kind == "order": pl.WRITE_ORDER = (1, 2, 5, 6, 15, 16, 17, 18, 21, 8, 9, 10, 11, 19, 20)
        if self.kind == "hash": pl.hash31_of_index = lambda i, f=pl.hash31_of_index: f(i) >> 1                                  # a hash lost one bit
        return self
    def __exit__(self, *a): pl.FIELDS, pl.MARKS, pl.key_of_index, pl.WRITE_ORDER, pl.hash31_of_index = self.save
def reference():
    return {"fields": pl.FIELDS, "order": pl.WRITE_ORDER, "unpack": pl.unpack, "pack": pl.pack, "is_legacy": pl.is_legacy, "legacy_unpack": pl.legacy_unpack,
            "migrate": lambda w, mx=pl.fixture_max: pl.migrate(w, mx), "migrate_resume": lambda w: pl.migrate_resume(w, pl.fixture_max)}
def resume_all_prefixes(exe):
    """a power loss after ANY number of the writes: the C core's resumed result equals the full migration, for layout 0 and layout 1, with tunings in both slots; the only allowed difference is the tuning slot a cut fell INSIDE of
    (its four writes are not atomic: that one tuning is lost, or, one time in 256, torn but check-valid), and a cut at a slot boundary loses nothing"""
    r = random.Random(33); bad = []; lines = []; want = []; ks = []; n_lost = 0
    for n in range(70):
        w = pl.random_v1_words(r) if n % 2 else words_v0(r)
        full = pl.migrate(w, pl.fixture_max)
        for k in range(len(pl.WRITE_ORDER) + 1):
            h = list(w)
            for word in pl.WRITE_ORDER[:k]: h[word] = full[word]
            lines.append("r %s\n" % " ".join(map(str, h))); want.append(full); ks.append(k)
    got = run(exe, FIX + "".join(lines))
    inside = {6: "B", 7: "B", 8: "B", 10: "A", 11: "A", 12: "A"}     # k writes done with a slot half written: B = after 1..3 of the writes of words 16, 17, 18, 21; A = after 1..3 of words 8..11
    for i, (g, wnt) in enumerate(zip(got, want)):
        lost = pl.resume_lost_slots(list(map(int, g.split())), wnt)
        if lost is None or (lost and lost != [inside.get(ks[i])]): bad.append(("prefix", i // (len(pl.WRITE_ORDER) + 1), ks[i], lost))
        n_lost += bool(lost)
    if not n_lost: bad.append("no interrupted slot was ever lost: the test does not exercise the in-slot case")
    if any(ks[i] not in inside and pl.resume_lost_slots(list(map(int, g.split())), want[i]) for i, g in enumerate(got)): bad.append("a cut at a slot boundary lost a tuning")
    return bad
with tempfile.TemporaryDirectory() as td:
    exe = build(Path(td))
    ref = reference(); ref["exe"] = exe
    bad = agree(ref); check("the Python reference equals the firmware's C code: table, write order, shipped meter ids, unpack, pack, legacy detection and unpack, migration from layout 0 and from layout 1 (with and without the tuning table), resumed migration after k writes (500 random register files)", not bad, bad[:3])
    bad = resume_all_prefixes(exe); check("the C core resumes a migration interrupted after ANY number of writes (70 cards x every prefix, layout 0 and 1, tunings in both slots) to the full migration", not bad, bad[:3])
    for m, nm in (("shift", "a wrong shift"), ("mark", "no converted mark on word 6"), ("key", "a tuning key without params_rev"), ("order", "word 5 written before word 6"), ("hash", "a hash that lost a bit")):
        with Mutant(m):
            r2 = reference(); r2["exe"] = exe
            caught = bool(agree(r2))
        check("mutant of the reference (%s) is caught" % nm, caught)
ia = {int(v["id"]): (int(v["address"], 16) - 0x20000000) // 4 for v in json.loads((ROOT / "dist/Cores/alfatreze.TAU/interact.json").read_text())["interact"]["variables"] if v.get("persist")}
check("id -> word equals the addresses of dist/Cores/alfatreze.TAU/interact.json", all(pl.ID_TO_WORD[i] == w for i, w in ia.items()) and set(ia) <= set(pl.ID_TO_WORD), str(ia))
z = ROOT / "release/alfatreze.TAU_0.6.1_2026-10-21.zip"
if z.exists():
    old = {int(v["id"]): (int(v["address"], 16) - 0x20000000) // 4 for v in json.loads(zipfile.ZipFile(z).read("Cores/alfatreze.TAU/interact.json"))["interact"]["variables"] if v.get("persist")}
    check("id -> word equals the addresses of the released v0.6.1 (ids 24-26 included)", all(pl.ID_TO_WORD[i] == w for i, w in old.items()), str(old))
g = pl.golden()
check("the committed golden vectors equal a fresh run", json.loads((ROOT / "docs/research/persist_layout/golden_vectors.json").read_text()) == json.loads(json.dumps(g)))
check("the golden resumed migrations (layout 0 and layout 1) all end at the full migration, except that a cut inside a tuning slot loses that slot", all(c["equals_full_migration"] or c["lost_tuning_slots"] for c in g["resume"] + g["resume_v1"]) and not any(c["lost_tuning_slots"] for c in g["resume"]) and any(c["lost_tuning_slots"] for c in g["resume_v1"]) and len(g["resume"]) >= 30 and len(g["resume_v1"]) >= 45)
check("the golden layout-1 cases keep tunings of shipped meters (some slot kept) and drop those of an index that is not one", any(any(c["tuning_slots_kept"]) for c in g["cases_v1"]) and not any(c["tuning_slots_kept"][0] for c in g["cases_v1"] if "not a shipped meter" in c["name"]))
cap = lambda n: json.loads((ROOT / f"docs/research/persist_layout/captures/dev{n}_interact_persist.json").read_text())
d181 = pl.describe(cap(181)); d184 = pl.describe(cap(184))
check("the real capture of TAU DEV 181 decodes as the legacy layout and migrates to a layout-2 register file with the same settings", d181["layout"] == "legacy" and pl.unpack(pl.migrate(pl.words_from_persist(cap(181))))["VOLUME"] == d181["values"]["VOLUME"] == 73 and d181["values"]["COLOUR"] == 7)
check("the real captures of TAU DEV 182, 183 and 184 decode as layout 1 (version 1, 16 variables)", all(pl.describe(cap(n))["layout"] == "v1" and len(cap(n)["interact_persist"]["variables"]) == 16 for n in (182, 183, 184)))
w185 = pl.words_from_persist(json.loads((ROOT / "docs/research/persist_layout/captures/dev185_after_migration_interact_persist.json").read_text()))
m185 = pl.migrate(w185, pl.fixture_max)
check("the real capture of TAU DEV 185 (a legacy card migrated to layout 1 on a Pocket) migrates to layout 2: meter 6 -> its id hash, preset position kept, last played and settings unchanged, version 2",
      pl.version(w185) == 1 and pl.unpack(m185)["METER_HASH"] == pl.hash31_of_index(6) and pl.unpack(m185)["METER_PRE_POS"] == 1 and pl.version(m185) == 2 and m185[19] == w185[19] and m185[20] == w185[20]
      and pl.unpack(m185)["VOLUME"] == pl.unpack(w185)["VOLUME"] and pl.unpack(m185)["COLOUR"] == pl.unpack(w185)["COLOUR"])
check("the staged captures (docs/research/persist_layout/staged) equal a fresh run and the README lists their SHA-256", pl.verify_staged(ROOT) == [], pl.verify_staged(ROOT))
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
