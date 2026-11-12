#!/usr/bin/env python3
"""Player 2 settings by area (fw2/cores/settings_core.h, docs/features/PERSISTENCE_LAYOUT.md): one register-file word per area (word 0 playback and layout version, 1 appearance, 2 audio, 5/6/15 meters, 8..11 and
16/17/18/21 the two tuning slots, 19/20 last played). The C table equals an independent statement of the layout (built here from the areas), unpack and pack equal an independent model over random register files,
no field overlaps another or reaches bit 31, every field fits the interact variable of its word and every variable's default is the packed default (a fresh card is on the new layout).
MIGRATION: every shipped legacy layout (TAU DEV 181 and older: volume word 0, repeat word 2, ReplayGain beside the polarity in word 17, presets in word 18 and 15, theme word 16 ...) converts to the same
settings in the new words, no legacy bit survives, the layout version is set, word 0 is the only marker; the new layout is never mistaken for a legacy one. Seven mutants must be caught.
LAYOUT 2 (external meters, P2.3): word 5 is the selected meter's id_hash (31 bits), word 6 its preset; the layout-1 -> 2 migration maps the old meter INDEX to the id hash of fw2/cores/shipped_meter_ids.h (placeholders until P2.0), keeps its preset, drops the other presets and re-keys the tunings."""
import json, random, re, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ((" " + str(extra)[:400]) if (extra and not ok) else ""))
    if not ok: fails += 1
# ---- the layout, stated independently of the C: (name, word, shift, width, min, max, default) ----
SHIPPED = [(int(i), int(h, 16), int(r)) for i, h, r in re.findall(r"X\((\d+),\s*0x([0-9A-Fa-f]+)u,\s*(\d+)u\)", (ROOT / "fw2/cores/shipped_meter_ids.h").read_text())]
def hash31(i): return SHIPPED[i][1] & 0x7FFFFFFF if i < len(SHIPPED) else 0
def model_table():
    t = [("VOLUME", 2, 0, 7, 0, 100, 94), ("REPEAT", 2, 7, 2, 0, 2, 0), ("RGMODE", 2, 9, 2, 0, 2, 0), ("HALCYON", 2, 11, 5, 0, 30, 0),
         ("COLOUR", 1, 0, 5, 0, 18, 1), ("THEME", 1, 5, 6, 0, 31, 0), ("POLARITY", 1, 11, 1, 0, 1, 0), ("METER_HASH", 5, 0, 31, 0, 0x7FFFFFFF, 0), ("FONT", 1, 12, 3, 0, 7, 0),
         ("LAYOUT", 0, 27, 4, 0, 15, 2), ("TUNE_NEW", 0, 26, 1, 0, 1, 0), ("METER_PRE_POS", 6, 0, 4, 0, 15, 0), ("METER_PRE_ID", 6, 4, 7, 0, 127, 0)]
    for k, w in enumerate((8, 9, 10, 11, 16, 17, 18, 21)): t.append(("TUNE%d" % k, w, 0, 31, 0, 0x7FFFFFFF, 0))
    t += [("METER", 0, 0, 0, 0, 0, 0)] + [("MPRE%d" % m, 0, 0, 0, 0, 0, 0) for m in range(7)] + [("MPID%d" % m, 0, 0, 0, 0, 0, 0) for m in range(7)]      # compatibility entries: no storage (width 0)
    t += [("LAST0", 19, 0, 31, 0, 0x7FFFFFFF, 0), ("LAST1", 20, 0, 31, 0, 0x7FFFFFFF, 0)]
    return t
TABLE = model_table(); NAMES = [r[0] for r in TABLE]; IDX = {n: i for i, n in enumerate(NAMES)}
PWORDS = (0, 1, 2, 5, 6, 8, 9, 10, 11, 15, 16, 17, 18, 19, 20, 21)
NEW = 1 << 30                                      # the mark of a word already converted to the new layout (no field reaches it)
MARKS = ((1, 30), (2, 30), (6, 29))                # word 1 and 2 bit 30 (layout 0 -> 2), word 6 bit 29 (the selected preset: converted to layout 2)
ORDER = [1, 2, 6, 5, 15, 16, 17, 18, 21, 8, 9, 10, 11, 19, 20]      # the write order: a source word is overwritten only after every new word built from it; word 0 (the version) last
BUILT_FROM = {1: (1, 16, 17, 18), 2: (0, 2, 6, 17), 6: (5, 15, 18), 5: (5,)}      # layout 0 sources (layout 1: word 6 from 5, 6, 15; word 5 from 5)
def m_unpack(w):
    out = []
    for _, word, sh, wd, lo, hi, df in TABLE:
        v = (w[word] >> sh) & ((1 << wd) - 1)
        out.append(v if lo <= v <= hi else df)
    return out
def m_pack(w, vals):
    w = list(w)
    for (_, word, sh, wd, lo, hi, df), v in zip(TABLE, vals):
        v = max(lo, min(hi, v)); m = ((1 << wd) - 1) << sh
        w[word] = (w[word] & ~m & 0xFFFFFFFF) | ((v << sh) & m)
    for k, b in MARKS: w[k] |= 1 << b
    return w
# ---- the legacy layout (TAU DEV 181 and older), stated independently: name -> (word, shift, width, min, max, default) ----
LEGACY = {"VOLUME": (0, 0, 32, 0, 100, 94), "REPEAT": (2, 0, 32, 0, 2, 0), "RGMODE": (17, 4, 2, 0, 2, 0), "HALCYON": (6, 0, 32, 0, 30, 0), "COLOUR": (1, 0, 32, 0, 18, 1), "THEME": (16, 0, 32, 0, 31, 0),
          "POLARITY": (17, 0, 1, 0, 1, 0), "METER": (5, 0, 32, 0, 14, 0), "MPRE0": (18, 0, 4, 0, 7, 0), "MPRE1": (18, 4, 4, 0, 7, 0), "MPRE2": (18, 8, 4, 0, 7, 0), "MPRE3": (18, 12, 4, 0, 15, 0),
          "MPRE4": (18, 16, 4, 0, 7, 0), "FONT": (18, 20, 3, 0, 7, 0), "MPRE5": (18, 23, 4, 0, 7, 0), "MPRE6": (15, 0, 4, 0, 15, 0), "LAST0": (19, 0, 31, 0, 0x7FFFFFFF, 0), "LAST1": (20, 0, 31, 0, 0x7FFFFFFF, 0)}
def legacy_words(settings, junk_tune=0):
    """a legacy register file holding `settings` (name -> value); the old tuning words 8..11 carry `junk_tune`"""
    w = [0] * 32
    for n, v in settings.items():
        word, sh, wd, lo, hi, df = LEGACY[n]; w[word] |= (v & ((1 << wd) - 1)) << sh
    for k in range(4): w[8 + k] = junk_tune
    return w
def expected_after(settings):
    """the settings a migrated card must hold: every legacy value (or its default when out of range), the rest default; the selected meter index becomes its id hash (0 = none past the shipped ones) and its preset position"""
    vals = [r[6] for r in TABLE]; leg = {}
    for n, v in settings.items():
        word, sh, wd, lo, hi, df = LEGACY[n]; leg[n] = v if lo <= v <= hi else df
        if n in IDX and TABLE[IDX[n]][3]: vals[IDX[n]] = leg[n]
    idx = leg.get("METER", 0)
    vals[IDX["METER_HASH"]] = hash31(idx)
    vals[IDX["METER_PRE_POS"]] = leg.get("MPRE%d" % idx, 0) if idx < 7 else 0
    return vals
def build(d, bug):
    exe = d / f"s{bug}"
    r = subprocess.run(["cc", "-O1", "-Wall", "-Werror", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_settings_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, text): return subprocess.run([str(exe)], input=text, capture_output=True, text=True).stdout.strip().split("\n")
def words(r):
    w = [0] * 32
    for i in PWORDS: w[i] = r.choice([0, 1, 2, 3, 94, 100, 101, r.getrandbits(31), r.getrandbits(8), 1 << 27])
    for i in range(32):
        if i not in PWORDS and r.random() < 0.3: w[i] = r.getrandbits(32)
    return w
def table_ok(exe):
    got = [tuple(map(int, l.split())) for l in run(exe, "T\n")]
    return got == [r[1:] for r in TABLE], len(got)
def agrees(exe):
    r = random.Random(11); cases = [words(r) for _ in range(800)]
    got = run(exe, "\n".join("u " + " ".join(map(str, w)) for w in cases) + "\n")
    if any(list(map(int, g.split())) != m_unpack(w) for g, w in zip(got, cases)): return False
    vs = [[r.randrange(0, hi + 3 if hi < 1000 else 1 << 32) for _, _, _, _, _, hi, _ in TABLE] for _ in cases]
    got = run(exe, "\n".join("p " + " ".join(map(str, w)) + " " + " ".join(map(str, v)) for w, v in zip(cases, vs)) + "\n")
    return all(list(map(int, g.split())) == m_pack(w, v) for g, w, v in zip(got, cases, vs))
def order_sane(exe):
    bad = []
    got = list(map(int, run(exe, "O\n")[0].split()))
    if got != ORDER: bad.append("write order %s, expected %s" % (got, ORDER))
    if sorted(got + [0]) != sorted({r[1] for r in TABLE} | {15}): bad.append("the write order plus word 0 must be exactly the words the table uses plus word 15 (emptied by the migration)")
    for target, srcs in BUILT_FROM.items():                                        # a legacy word is overwritten only after every word built from it has been written
        for src in srcs:
            if src != target and src in ORDER and ORDER.index(src) < ORDER.index(target): bad.append("word %d is overwritten before word %d, which is built from it" % (src, target))
    return bad
def layout_sane():
    bad = []
    used = {}
    for n, word, sh, wd, lo, hi, df in TABLE:
        if sh + wd > 31: bad.append("%s reaches bit %d (an interact variable is 31 bits)" % (n, sh + wd))
        if hi >= (1 << wd): bad.append("%s max %d does not fit %d bits" % (n, hi, wd))
        if not lo <= df <= hi: bad.append("%s default %d outside %d..%d" % (n, df, lo, hi))
        for b in range(sh, sh + wd):
            if (word, b) in used: bad.append("%s overlaps %s (word %d bit %d)" % (n, used[(word, b)], word, b))
            used[(word, b)] = n
    return bad
def interact_vars():
    d = json.loads((ROOT / "dist/Cores/alfatreze.TAU/interact.json").read_text())
    return {int(v["address"], 16) // 4 % 32: v for v in d["interact"]["variables"] if v.get("persist")}
def fits_interact():
    """Every persisted word has exactly one interact variable that can hold its fields (a value above the variable's max is refused by the Pocket: the 32-bit last-played words were, and never came back), at most 16
    variables exist, and each variable's default is the packed default of its word (a fresh card is on the new layout, not read as a legacy one)."""
    lim = interact_vars(); bad = []
    if len(lim) > 16: bad.append("%d persisted variables (the Pocket takes 16)" % len(lim))
    defaults = m_pack([0] * 32, [r[6] for r in TABLE])
    for word in sorted({r[1] for r in TABLE} | {15}):               # word 15 has no field in layout 2 (free) but its variable stays declared: ids never change
        v = lim.get(word)
        if v is None: bad.append("word %d has no persisted interact variable" % word); continue
        g = v["graphical"]; top = max([((hi << sh) if wd < 31 else hi) for _, w, sh, wd, lo, hi, df in TABLE if w == word] or [0])
        if g["max"] < ((1 << 31) - 1) and top > g["max"]: bad.append("word %d: a field reaches %d, variable %d allows %d" % (word, top, v["id"], g["max"]))
        if v["defaultval"] != defaults[word]: bad.append("word %d: variable %d defaults to %d, the packed defaults are %d" % (word, v["id"], v["defaultval"], defaults[word]))
    for word in lim:
        if word not in {r[1] for r in TABLE} | {15}: bad.append("variable %d carries word %d which no field uses" % (lim[word]["id"], word))
    return bad
def props(exe):
    ok = {}
    r = random.Random(5)
    junk = [0] * 32; junk[2] = 127 | (3 << 7) | (3 << 9) | (31 << 11)                                  # volume 127, repeat 3, ReplayGain 3, Halcyon 31: all out of range
    ok["out-of-range values fall back to defaults"] = run(exe, "u " + " ".join(map(str, junk)) + "\n")[0].split()[:4] == ["94", "0", "0", "0"]
    base = m_pack([0] * 32, [r_[6] for r_ in TABLE]); foreign = list(base); foreign[3] = 12345; foreign[30] = 7                     # words nobody owns
    vals = [min(r_[5], 3) for r_ in TABLE]
    p = list(map(int, run(exe, "p " + " ".join(map(str, foreign)) + " " + " ".join(map(str, vals)) + "\n")[0].split()))
    ok["pack keeps every word that is not ours"] = p[3] == 12345 and p[30] == 7
    ok["pack then unpack is the identity"] = list(map(int, run(exe, "u " + " ".join(map(str, p)) + "\n")[0].split())) == [max(r_[4], min(r_[5], v)) for r_, v in zip(TABLE, vals)]
    ok["the default register file reads as defaults and is the new layout"] = (run(exe, "g " + " ".join(map(str, base)) + "\n")[0] == "0"
        and list(map(int, run(exe, "u " + " ".join(map(str, base)) + "\n")[0].split())) == [r_[6] for r_ in TABLE])
    ok["a legacy register file (volume 0..100 in word 0) is recognised as legacy"] = all(run(exe, "g " + " ".join(map(str, legacy_words({"VOLUME": v}))) + "\n")[0] == "1" for v in (0, 1, 50, 94, 100))
    return ok
def migration(exe):
    r = random.Random(21); bad = []
    for n in range(300):
        s = {}
        for name, (word, sh, wd, lo, hi, df) in LEGACY.items():
            if name in ("LAST0", "LAST1"): s[name] = r.getrandbits(31)
            elif r.random() < 0.15: s[name] = r.randrange(hi + 1, min(hi + 40, (1 << min(wd, 31)) - 1) + 1) if hi + 1 < (1 << min(wd, 31)) else hi   # out of range
            else: s[name] = r.randint(lo, hi)
        junk = r.choice([0, r.getrandbits(31)])
        w = legacy_words(s, junk)
        mig = list(map(int, run(exe, "m " + " ".join(map(str, w)) + "\n")[0].split()))
        want = expected_after(s)
        if m_unpack(mig) != want: bad.append(("values", n, s)); continue
        if mig[0] >> 27 != 2: bad.append(("layout version", n)); continue
        if any(mig[k] != 0 for k in (8, 9, 10, 11, 15, 16, 17, 18, 21)): bad.append(("tuning or word 15 not emptied (the legacy header differs, a stale record must not be read)", n)); continue
        if (mig[6] >> 11) & 63 != min(s.get("METER", 0) if 0 <= s.get("METER", 0) <= 14 else 0, 63) or not mig[6] & (1 << 29): bad.append(("word 6 scratch index or mark", n)); continue
        if any(mig[k] for k in range(32) if k not in PWORDS): bad.append(("foreign word written", n)); continue
        if run(exe, "g " + " ".join(map(str, mig)) + "\n")[0] != "0": bad.append(("still legacy", n)); continue
        got = list(map(int, run(exe, "l " + " ".join(map(str, w)) + "\n")[0].split()))
        if got != want: bad.append(("legacy unpack", n))
    # a power loss after ANY number of the writes of the migration: the next boot resumes and ends with the same register file
    for n in range(60):
        s = {name: r.randint(lo, hi) for name, (word, sh, wd, lo, hi, df) in LEGACY.items() if name not in ("LAST0", "LAST1")}
        s["LAST0"] = r.getrandbits(31); s["LAST1"] = r.getrandbits(31)
        w = legacy_words(s, r.getrandbits(31)); full = list(map(int, run(exe, "m " + " ".join(map(str, w)) + "\n")[0].split()))
        for k in range(len(ORDER) + 1):
            half = list(w)
            for word in ORDER[:k]: half[word] = full[word]                         # the first k writes happened, word 0 (the legacy marker) not yet
            resumed = list(map(int, run(exe, "r " + " ".join(map(str, half)) + "\n")[0].split()))
            if resumed != full or run(exe, "g " + " ".join(map(str, half)) + "\n")[0] != "1": bad.append(("interrupted after %d writes" % k, n)); break
    return bad
def v1_words(r, idx, pos, pid, junk_other=True):
    """a layout-1 register file, written here independently: version 1, the marks, the selected meter index, preset positions of the seven meters (word 5 bits 6.., word 6 bits 0..3) and preset ids (word 6 from bit 4, word 15)"""
    w = [0] * 32
    w[0] = 1 << 27 | r.randint(0, 1) << 26; w[1] = (1 << 30) | r.randint(0, 18) | r.randint(0, 31) << 5 | r.randint(0, 1) << 11 | r.randint(0, 7) << 12; w[2] = (1 << 30) | r.randint(0, 100) | r.randint(0, 2) << 7 | r.randint(0, 2) << 9 | r.randint(0, 30) << 11
    w[5] = (1 << 30) | idx; w[6] = 1 << 30; w[15] = 1 << 30
    for m in range(7):
        if m < 6: w[5] |= pos[m] << (6 + 4 * m)
        else: w[6] |= pos[m]
        if m < 3: w[6] |= pid[m] << (4 + 7 * m)
        else: w[15] |= pid[m] << (7 * (m - 3))
    w[19] = r.getrandbits(31); w[20] = r.getrandbits(31)
    return w
def v1_tuning(meter, preset, entries, mx):
    """a layout-1 record (check 8 | meter 5 | preset 7 | count 4 | entries from bit 24, seed 0x5A) as four 31-bit words"""
    bits = [0] * 128; pos = 24
    def put(p, v, n):
        for i in range(n): bits[p + i] = (v >> i) & 1
        return p + n
    nb = lambda m: next(n for n in range(1, 17) if n == 16 or not (m >> n))
    for i, v in entries: pos = put(pos, i, 6); pos = put(pos, v, nb(mx[i]))
    put(8, meter, 5); put(13, preset, 7); put(20, len(entries), 4)
    by = [sum(bits[8 * k + j] << j for j in range(8)) for k in range(16)]; c = 0x5A
    for k in range(1, 16): c = ((c << 1) | (c >> 7)) & 255; c = (c + by[k]) & 255
    put(0, c, 8)
    return [sum(bits[31 * k + j] << j for j in range(31)) for k in range(4)]
def migration_v1(exe):
    """layout 1 -> 2: the selected index becomes the id hash, its preset (position, file id + 1) moves to word 6, the other meters' presets are dropped, word 15 is emptied, every other setting and the last played words are kept, the
    tunings are re-keyed by the shipped ids (low 16 bits of id_hash, low 4 of params_rev); an index that is not a shipped meter selects nothing"""
    r = random.Random(41); bad = []
    mxs = {2: [10, 255, 3, 65535], 5: [1, 1, 100]}
    fx = "".join("M %d %d %s\n" % (m, len(mx), " ".join(map(str, mx))) for m, mx in mxs.items())
    for n in range(120):
        idx = r.choice([r.randint(0, 6)] * 3 + [r.randint(7, 62)]); pos = [r.randint(0, 15) for _ in range(7)]; pid = [r.randint(0, 127) for _ in range(7)]
        w = v1_words(r, idx, pos, pid)
        tm = r.choice([2, 5, 9]); ent = [(0, 3), (3, 40000)] if tm == 2 else [(0, 1), (2, 77)] if tm == 5 else [(0, 1)]
        if tm in mxs: rec = v1_tuning(tm, 6, [e for e in ent if e[0] < len(mxs[tm])], mxs[tm])
        else: rec = v1_tuning(tm, 6, ent, [255])
        for k, v in zip((8, 9, 10, 11), rec): w[k] = v
        mig = list(map(int, run(exe, fx + "m " + " ".join(map(str, w)) + "\n")[0].split()))
        v = m_unpack(mig)
        sel_pos = pos[idx] if idx < 7 else 0; sel_id = pid[idx] if idx < 7 else 0
        if mig[0] >> 27 != 2 or mig[0] & (1 << 26) != w[0] & (1 << 26): bad.append(("version or tune_new bit", n)); continue
        if v[IDX["METER_HASH"]] != hash31(idx) or mig[5] != hash31(idx): bad.append(("hash", n, idx)); continue
        if v[IDX["METER_PRE_POS"]] != sel_pos or v[IDX["METER_PRE_ID"]] != sel_id: bad.append(("selected preset", n)); continue
        if mig[15] != 0 or (mig[6] >> 11) & 63 != idx or not mig[6] & (1 << 29): bad.append(("word 15 or word 6 scratch/mark", n)); continue
        if any(mig[k] != w[k] for k in (1, 2, 19, 20)): bad.append(("an unchanged word changed", n)); continue
        if any(mig[k] for k in (16, 17, 18, 21)): bad.append(("empty slot B grew", n)); continue
        key = ((SHIPPED[tm][1] & 0xFFFF) | ((SHIPPED[tm][2] & 15) << 16)) if tm < 7 else None
        kept = key is not None and tm in mxs
        if kept != (mig[8] != 0): bad.append(("tuning kept/dropped", n, tm)); continue
        if kept and (mig[8] >> 8) & 0xFFFFF != key: bad.append(("tuning key", n, tm, hex((mig[8] >> 8) & 0xFFFFF), hex(key))); continue
        if run(exe, "g " + " ".join(map(str, w)) + "\n")[0] != "0": bad.append(("layout 1 is not legacy", n))
    return bad
with tempfile.TemporaryDirectory() as td:
    bad = layout_sane(); check("the layout is sane: no field overlaps another, nothing reaches bit 31, every default is in range", not bad, bad)
    bad = fits_interact(); check("every word has an interact variable that holds it, 16 variables at most, each default is the packed default", not bad, bad)
    d = Path(td); g = build(d, 0)
    ok, n = table_ok(g); check("the C table equals the independent statement of the layout (%d fields)" % n, ok)
    check("C core equals the independent model over 800 random register files (unpack and pack)", agrees(g))
    for k, v in props(g).items(): check(k, v)
    bad = migration(g); check("migration: 300 random legacy cards convert to the same settings, the tuning slots are emptied, the version in word 0 marks the layout, and a migration interrupted after any of its writes is resumed to the same register file", not bad, bad[:2])
    bad = order_sane(g); check("the write order protects the legacy words (each is overwritten only after every new word built from it) and covers every word", not bad, bad)
    for bug, name in ((1, "shift off by one"), (2, "no mask"), (3, "no range check"), (4, "pack clobbers neighbours")):
        b = build(d, bug); check(f"mutant {bug} ({name}) is caught", (not agrees(b)) or not all(props(b).values()))
    b = build(d, 5); check("mutant 5 (a legacy card is not migrated) is caught", bool(migration(b)) or not all(props(b).values()))
    b = build(d, 6); check("mutant 6 (a resumed migration rebuilds words that are already converted) is caught", bool(migration(b)))
    bad = migration_v1(g); check("migration from layout 1: 120 random cards (the selected index becomes the shipped id hash, its preset moves to word 6, the others are dropped, word 15 is emptied, the rest is kept, tunings are re-keyed or dropped)", not bad, bad[:2])
    for bug, name in ((7, "the hash loses a bit"), (8, "the tuning key forgets params_rev")):
        b = build(d, bug); check(f"mutant {bug} ({name}) is caught", bool(migration_v1(b)) or bool(migration(b)))
print("FAILED" if fails else "all ok", fails); sys.exit(1 if fails else 0)
