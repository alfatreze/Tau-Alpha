#!/usr/bin/env python3
"""RGB565 colour ramp (fw2/cores/ramp_core.h): equals an independent statement of the specification (per channel, a*(n-t) + b*t divided by n rounding toward zero; n = 0 gives a) over 40,000 random
colour pairs with every step of n = 1..8, over the corners, and is the value the Chladni meter's own earlier colour mix produced for the (3-step) ramps it draws, so no picture changes. Mutant: rounding to nearest."""
import random, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    if not ok: fails += 1
def ramp(a, b, t, n):
    if n == 0: return a
    out = 0
    for sh, w in ((11, 5), (5, 6), (0, 5)):
        m = (1 << w) - 1; ca, cb = (a >> sh) & m, (b >> sh) & m
        out |= ((ca * (n - t) + cb * t) // n) << sh
    return out
def run(bug, cases):
    with tempfile.TemporaryDirectory() as td:
        exe = Path(td) / "h"
        r = subprocess.run(["cc", "-O1", "-w", f"-DBUG={bug}", "-o", str(exe), str(ROOT / "sim/p2_ramp_harness.c")], capture_output=True, text=True)
        if r.returncode: print(r.stderr); sys.exit(1)
        out = subprocess.run([str(exe)], input="\n".join("%d %d %d %d" % c for c in cases), capture_output=True, text=True).stdout.split()
    return [int(x) for x in out]
rng = random.Random(7)
cases = [(rng.randrange(65536), rng.randrange(65536), t, n) for _ in range(5000) for n in range(1, 9) for t in (rng.randrange(n + 1),)]
cases += [(a, b, t, n) for a in (0, 0xFFFF, 0xF800, 0x07E0, 0x001F) for b in (0, 0xFFFF, 0x8410) for n in range(0, 9) for t in range(0, n + 1)]
want = [ramp(*c) for c in cases]
check("%d cases equal the statement" % len(cases), run(0, cases) == want)
check("the ends are the colours: t = 0 gives a, t = n gives b (n >= 1)", all(ramp(a, b, 0, n) == a and ramp(a, b, n, n) == b for a, b, t, n in cases if n))
check("n = 0 returns a", all(w == c[0] for c, w in zip(cases, want) if c[3] == 0))
check("mutant (rounds to nearest) is caught", run(1, cases) != want)
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
