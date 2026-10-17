#!/usr/bin/env python3
"""Player 2 settings table (fw2/cores/settings_core.h): the C core equals an independent statement of the table over random register files; the LEGACY layout of the old player
(volume word 0, repeat word 2, ReplayGain mode in bits 5:4 of word 17 beside the theme polarity bit) reads back exactly; out-of-range values fall back to defaults; pack keeps every
bit that is not its own (another session's theme polarity, retired words); pack then unpack is the identity. Four mutants must be caught."""
import random, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ((" " + str(extra)) if (extra and not ok) else ""))
    if not ok: fails += 1
TABLE = [(0, 0, 32, 0, 100, 94), (2, 0, 32, 0, 2, 0), (17, 4, 2, 0, 2, 0), (6, 0, 32, 0, 30, 0), (1, 0, 32, 0, 18, 1), (16, 0, 32, 0, 31, 0), (17, 0, 1, 0, 1, 0), (5, 0, 32, 0, 14, 0), (18, 0, 4, 0, 7, 0), (18, 4, 4, 0, 7, 0), (18, 8, 4, 0, 7, 0), (18, 12, 4, 0, 15, 0), (18, 16, 4, 0, 7, 0), (8, 0, 31, 0, 0x7FFFFFFF, 0), (9, 0, 31, 0, 0x7FFFFFFF, 0), (10, 0, 31, 0, 0x7FFFFFFF, 0), (11, 0, 31, 0, 0x7FFFFFFF, 0), (18, 20, 3, 0, 7, 0)]
def m_unpack(w):
    out = []
    for word, sh, wd, lo, hi, df in TABLE:
        v = (w[word] >> sh) & (0xFFFFFFFF if wd == 32 else (1 << wd) - 1)
        out.append(v if lo <= v <= hi else df)
    return out
def m_pack(w, vals):
    w = list(w)
    for (word, sh, wd, lo, hi, df), v in zip(TABLE, vals):
        v = max(lo, min(hi, v)); m = (0xFFFFFFFF if wd == 32 else (1 << wd) - 1) << sh
        w[word] = (w[word] & ~m & 0xFFFFFFFF) | ((v << sh) & m)
    return w
def build(d, bug):
    exe = d / f"s{bug}"
    r = subprocess.run(["cc", "-O1", "-Wall", "-Werror", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_settings_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, text): return subprocess.run([str(exe)], input=text, capture_output=True, text=True).stdout.strip().split("\n")
def words(r):
    w = [0] * 32
    for i in (0, 1, 2, 5, 6, 8, 9, 10, 11, 16, 17, 18): w[i] = r.choice([0, 1, 2, 3, 94, 100, 101, r.getrandbits(32), r.getrandbits(8)])
    for i in range(32):
        if i not in (0, 1, 2, 5, 6, 8, 9, 10, 11, 16, 17, 18) and r.random() < 0.3: w[i] = r.getrandbits(32)
    return w
def agrees(exe):
    r = random.Random(11); cases = [words(r) for _ in range(800)]
    got = run(exe, "\n".join("u " + " ".join(map(str, w)) for w in cases) + "\n")
    if any(list(map(int, g.split())) != m_unpack(w) for g, w in zip(got, cases)): return False
    vs = [[r.randrange(0, 130), r.randrange(0, 5), r.randrange(0, 5), r.randrange(0, 40), r.randrange(0, 25), r.randrange(0, 35), r.randrange(0, 3), r.randrange(0, 18), r.randrange(0, 6), r.randrange(0, 6), r.randrange(0, 5), r.randrange(0, 9), r.randrange(0, 4), r.getrandbits(31), r.getrandbits(31), r.getrandbits(32), r.getrandbits(31), r.randrange(0, 9)] for _ in cases]
    got = run(exe, "\n".join("p " + " ".join(map(str, w)) + " " + " ".join(map(str, v)) for w, v in zip(cases, vs)) + "\n")
    return all(list(map(int, g.split())) == m_pack(w, v) for g, w, v in zip(got, cases, vs))
def props(exe):
    ok = {}
    legacy = [0] * 32; legacy[0] = 80; legacy[2] = 1; legacy[6] = 5; legacy[17] = 0x21                # volume 80, repeat all, polarity 1, RG mode 2 (album)
    ok["legacy words read back exactly"] = run(exe, "u " + " ".join(map(str, legacy)) + "\n")[0].split() == ["80", "1", "2", "5", "0", "0", "1", "0", "0", "0", "0", "0", "0", "0", "0", "0", "0", "0"]
    junk = [0] * 32; junk[0] = 4000; junk[2] = 7; junk[6] = 31; junk[17] = 0x30                      # volume out of range, repeat 7, RG mode 3
    ok["out-of-range values fall back to defaults"] = run(exe, "u " + " ".join(map(str, junk)) + "\n")[0].split() == ["94", "0", "0", "0", "0", "0", "0", "0", "0", "0", "0", "0", "0", "0", "0", "0", "0", "0"]
    p = run(exe, "p " + " ".join(map(str, legacy)) + " 55 2 1 7 5 3 1 4 2 3 1 5 2 123456789 0 2147483647 7 3\n")[0].split()
    ok["pack keeps the theme polarity bit and every foreign word"] = int(p[17]) & 1 == 1 and int(p[17]) == (0x21 & ~0x30 & 0xFFFFFFFF | (1 << 4)) and all(p[i] == "0" for i in range(32) if i not in (0, 1, 2, 5, 6, 8, 9, 10, 11, 16, 17, 18))
    ok["pack then unpack is the identity"] = run(exe, "u " + " ".join(p) + "\n")[0].split() == ["55", "2", "1", "7", "5", "3", "1", "4", "2", "3", "1", "5", "2", "123456789", "0", "2147483647", "7", "3"]
    return ok
with tempfile.TemporaryDirectory() as td:
    d = Path(td); g = build(d, 0)
    check("C core equals the independent table over 800 random register files (unpack and pack)", agrees(g))
    for k, v in props(g).items(): check(k, v)
    for bug, name in ((1, "shift off by one"), (2, "no mask"), (3, "no range check"), (4, "pack clobbers neighbours")):
        b = build(d, bug); check(f"mutant {bug} ({name}) is caught", (not agrees(b)) or not all(props(b).values()))
print("FAILED" if fails else "all ok", fails); sys.exit(1 if fails else 0)
