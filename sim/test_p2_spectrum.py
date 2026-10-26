#!/usr/bin/env python3
"""Player 2 spectrum arithmetic (fw2/cores/spectrum_core.h): the log scale (16 units per octave), the per-band level (mirrored gain, floor, span, clamp) and the ballistics against independent Python statements; two mutants must be caught."""
import math, random, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ((" " + str(extra)) if (extra and not ok) else ""))
    if not ok: fails += 1
GAIN = [1428, 411, 788, 266, 506, 228, 411, 255, 430, 345, 521, 496, 699, 764, 1010, 1312]
def m_log(v):
    if v == 0: return 0
    e = v.bit_length() - 1
    m = ((v >> (e - 4)) & 15) if e >= 4 else ((v << (4 - e)) & 15)
    return (e << 4) | m
def m_level(mean, b):
    lg = m_log((mean * GAIN[15 - b]) >> 4)
    v = (lg - 200) * 255 // 50 if lg > 200 else 0
    return min(v, 255)
def m_ball(cur, v): return v if v >= cur else cur - ((cur - v) // 4 + 1)
def build(d, bug):
    exe = d / f"p{bug}"
    r = subprocess.run(["cc", "-O1", "-Wall", "-Werror", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_spec_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def agrees(exe):
    r = random.Random(9); cases = []; want = []
    for _ in range(600):
        v = r.choice([0, 1, 15, 16, 255, r.getrandbits(r.randrange(1, 21))]); cases.append(f"l {v} 0"); want.append(m_log(v))
        mean = r.getrandbits(r.randrange(1, 21)); b = r.randrange(16); cases.append(f"v {mean} {b}"); want.append(m_level(mean, b))
        c, n = r.randrange(256), r.randrange(256); cases.append(f"b {c} {n}"); want.append(m_ball(c, n))
    got = subprocess.run([str(exe)], input="\n".join(cases) + "\n", capture_output=True, text=True).stdout.split()
    return list(map(int, got)) == want
with tempfile.TemporaryDirectory() as td:
    d = Path(td)
    check("log scale, band level and ballistics equal the independent statements (1800 cases)", agrees(build(d, 0)))
    check("the log scale is 16 units per octave", all(m_log(2 * v) - m_log(v) == 16 for v in (16, 100, 5000)))
    for bug, name in ((1, "gain table not mirrored"), (2, "no release smoothing")):
        check(f"mutant {bug} ({name}) is caught", not agrees(build(d, bug)))
print("FAILED" if fails else "all ok", fails); sys.exit(1 if fails else 0)
