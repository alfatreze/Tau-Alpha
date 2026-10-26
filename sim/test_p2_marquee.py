#!/usr/bin/env python3
"""The Now Playing text scroll (fw2/cores/marquee_core.h) against independent statements of its timing: rest 1,800 ms, one character per 120 ms until the tail fits, hold 1,800 ms, start over, and a new
string starts again at its first character; two mutants (no initial pause, never starts over) must be caught."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(a, b):
    global fails
    name, ok = (a, b) if isinstance(a, str) else (b, a)
    print(("ok   " if ok else "FAIL ") + name)
    if not ok: fails += 1
def run(bug):
    with tempfile.TemporaryDirectory() as td:
        exe = Path(td) / "h"
        r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", f"-DBUG={bug}", "-o", str(exe), str(ROOT / "sim/p2_marquee_harness.c")], capture_output=True, text=True)
        if r.returncode: print(r.stderr); sys.exit(1)
        out = subprocess.run([str(exe)], capture_output=True, text=True).stdout.split("\n")
    pts = {}; new = None
    for l in out:
        a = l.split()
        if len(a) == 2 and a[0] == "new": new = int(a[1])
        elif len(a) == 2: pts[int(a[0])] = int(a[1])
    return pts, new
pts, new = run(0)
check(pts[0] == 0 and pts[1700] == 0, "it rests on the first character for 1.8 s")
check(pts[1900] == 1, "then moves one character")
check(pts[3100] == 11 or pts[3100] == 10, f"about one character per 120 ms (k at 3.1 s = {pts[3100]})")
check(max(pts.values()) == 20, f"it stops when the last 10 characters fit (k max {max(pts.values())}, 30 chars in a 10-character box)")
end_t = min(t for t, k in pts.items() if k == 20)
check(pts[end_t + 1700] == 20 and pts[end_t + 1900] == 0, f"it holds the end for 1.8 s (k=20 from {end_t} ms) and starts over")
check(new == 0, "a new string starts at its first character")
for bug, name, test in ((1, "no pause before it starts", lambda p, n: p[200] != 0), (2, "never starts over", lambda p, n: p[9000] == 20 and max(p.values()) == 20 and p[8000] == 20)):
    p, n = run(bug); check(f"mutant '{name}' is caught", test(p, n))
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
