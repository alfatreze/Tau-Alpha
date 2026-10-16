#!/usr/bin/env python3
"""Halcyon selection rules (fw2/cores/halcyon_sel_core.h): the cycle on Y, the preset step on L1/R1, restoring a saved value, what is saved, the page rows and control ranges."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(extra)))
    if not ok: fails += 1
def build(td, bug):
    exe = Path(td) / f"h{bug}"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_halcyon_sel_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def ask(exe, text): return subprocess.run([str(exe)], input=text, capture_output=True, text=True).stdout.split()
Q = "Y 0 8\nY 8 8\nY 200 8\nY 3 10\nY 10 10\nS 0 8 1\nS 8 8 1\nS 3 8 -1\nS 1 8 -1\nS 200 8 1\nS 200 8 -1\nS 9 10 1\nR 5 8\nR 9 8\nR 200 10\nR 31 16\nV 5 3\nV 200 3\nV 0 3\nW 0 1\nW 6 0\nW 3 0\nC 5 -3\nC 5 9\nC 1 -9\nC 1 9\nC 2 3\n"
with tempfile.TemporaryDirectory() as td:
    g = build(td, 0)
    o = ask(g, Q)
    want = ["1", "0", "1", "4", "0", "1", "1", "2", "8", "1", "8", "10", "5", "0", "0", "0", "5", "3", "0", "6", "5", "2", "0", "5", "-5", "5", "3"]
    check("Y: next preset, after the last back to OFF, from CUSTOM it restarts at OFF, user presets count", o[0:5] == ["1", "0", "1", "4", "0"], o[0:5])
    check("L1/R1: next/previous preset, never OFF, wraps, from OFF or CUSTOM first/last", o[5:12] == ["1", "1", "2", "8", "1", "8", "10"], o[5:12])
    check("a saved or Core Settings value that is not a preset of this build means OFF", o[12:16] == ["5", "0", "0", "0"], o[12:16])
    check("saving: a preset is saved; CUSTOM keeps what was on file", o[16:19] == ["5", "3", "0"], o[16:19])
    check("page rows wrap over the preset area and the six controls", o[19:22] == ["6", "0", "4"], o[19:22])
    check("control ranges: sibilance 0..5, the others -5..5", o[22:27] == ["0", "5", "-5", "5", "3"], o[22:27])
    for bug, what, probe in ((1, "the cycle skips OFF", "Y 8 8\n"), (2, "a stale saved value is accepted", "R 31 16\nR 9 8\n"), (3, "CUSTOM overwrites the preset on file", "V 200 3\n")):
        check(f"mutant {bug} ({what}) is caught", ask(build(td, bug), probe) != ask(g, probe))
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
