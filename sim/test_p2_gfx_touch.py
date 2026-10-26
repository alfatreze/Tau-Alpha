#!/usr/bin/env python3
"""The rows a run of draw commands touched (fw2/cores/gfx_touch_core.h): visible rows and stash rows are kept apart, a command that runs from the screen into the stash splits at row 360, spans grow to cover every command,
a reset empties them, a zero-height command touches nothing. Mutants: no clip at row 360; the stash span starts at the command's own row."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    if not ok: fails += 1
SCRIPT = "N 100 20  N 50 10  N 300 30  R  N 780 200  N 790 5  N 340 40  N 0 0  R  N 400 1  N 359 1  N 360 1"
def run(bug):
    with tempfile.TemporaryDirectory() as td:
        exe = Path(td) / "h"
        r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DBUG=%d" % bug, "-o", str(exe), str(ROOT / "sim/p2_gfx_touch_harness.c")], capture_output=True, text=True)
        if r.returncode: print(r.stderr); sys.exit(1)
        return subprocess.run([str(exe)], input=SCRIPT, capture_output=True, text=True).stdout.strip().split("\n")
GOOD = ["vis 100-120 stash 65535-0 visible 1", "vis 50-120 stash 65535-0 visible 1", "vis 50-330 stash 65535-0 visible 1", "vis 65535-0 stash 65535-0 visible 0",
        "vis 65535-0 stash 780-980 visible 0", "vis 65535-0 stash 780-980 visible 0", "vis 340-360 stash 360-980 visible 1", "vis 340-360 stash 360-980 visible 1", "vis 65535-0 stash 65535-0 visible 0",
        "vis 65535-0 stash 400-401 visible 0", "vis 359-360 stash 400-401 visible 1", "vis 359-360 stash 360-401 visible 1"]   # the 340+40 command crosses row 360 and splits there
out = run(0)
for g, o in zip(GOOD, out): check(g, g == o)
check("every step printed", len(out) == len(GOOD))
check("mutant (no clip at row 360) is caught", run(1) != GOOD)
check("mutant (the stash span starts at the command's own row) is caught", run(2) != GOOD)
print("PASSED" if not fails else "FAILED (%d)" % fails)
sys.exit(1 if fails else 0)
