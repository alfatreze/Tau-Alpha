#!/usr/bin/env python3
"""Player 2 gain target (fw2/cores/gain_core.h): volume taper x ReplayGain, capped, from the library details fields. Checked against an independent statement of the rules
(10^(cdb/2000), mode/fallback choice, peak cap 0.944/peak limited to unity..+6 dB, attenuate-only without a peak) over random cases; three mutants must be caught."""
import math, random, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ((" " + str(extra)) if (extra and not ok) else ""))
    if not ok: fails += 1
# the volume taper: 10^((v-100)*0.6/20) in Q15 (v=0 mute)
def vol(v): return 0 if v == 0 else round(32768 * 10 ** ((v - 100) * 0.6 / 20))
def model(mode, flags, tg, ag, tp, ap, boost, v):
    have = (flags >> 4) & 15
    if mode == 2 and have & 2: cdb, peak, hp = ag, ap, have & 8
    elif mode != 0 and have & 1: cdb, peak, hp = tg, tp, have & 4
    elif mode != 0 and have & 2: cdb, peak, hp = ag, ap, have & 8
    else: cdb, peak, hp = None, 0, 0
    factor = 32768 if cdb is None else 32768 * 10 ** (max(-3000, min(1800, cdb)) / 2000)
    pk = peak if hp else 0
    cap = 32768 if (not boost or pk == 0) else min(65535, max(32768, 30934 * 32768 / pk))
    t = min(vol(v) * min(factor, 262144) / 32768, cap)
    return factor, cap, t
def build(d, bug):
    exe = d / f"g{bug}"
    r = subprocess.run(["cc", "-O1", "-Wall", "-Wno-unused-function", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_gain_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def cases():
    r = random.Random(3); out = []
    for _ in range(3000):
        flags = r.randrange(16) << 4
        out.append((r.randrange(3), flags, r.randrange(-2500, 1200), r.randrange(-2500, 1200), r.choice([0, r.randrange(2000, 40000)]), r.choice([0, r.randrange(2000, 40000)]), r.randrange(2), r.choice([0, 1, 50, 94, 100, r.randrange(101)])))
    return out
def agrees(exe):
    cs = cases(); inp = "\n".join(" ".join(map(str, c)) for c in cs) + "\n"
    got = subprocess.run([str(exe)], input=inp, capture_output=True, text=True).stdout.split("\n")
    bad = 0
    for c, line in zip(cs, got):
        f, cap, t = map(int, line.split()); mf, mc, mt = model(*c)
        if abs(f - mf) > max(3, mf * 0.006) or abs(cap - mc) > max(2, mc * 0.002) or abs(t - mt) > max(3, mt * 0.01): bad += 1
    return bad == 0
with tempfile.TemporaryDirectory() as td:
    d = Path(td)
    check("target equals the independent rules over 3000 random cases", agrees(build(d, 0)))
    for bug, name in ((1, "tag flag bits misread"), (2, "peak cap ignored"), (3, "result not capped")):
        check(f"mutant {bug} ({name}) is caught", not agrees(build(d, bug)))
print("FAILED" if fails else "all ok", fails); sys.exit(1 if fails else 0)
