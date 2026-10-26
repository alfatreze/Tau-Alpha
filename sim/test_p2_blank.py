#!/usr/bin/env python3
"""Screen blank core (fw2/cores/blank_core.h): with a 1 minute setting the screen sleeps after 60 idle seconds (not before), a press restarts the count, the press that wakes it is reported so it can be swallowed, 'off' never sleeps,
and the count survives the cycle counter wrapping. Mutant: a second counts as a minute."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    if not ok: fails += 1
HZ = 1000
def build(td, bug):
    exe = Path(td) / f"b{bug}"
    r = subprocess.run(["cc", "-O1", "-Wall", "-Werror", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_blank_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, lines):
    return [tuple(map(int, l.split())) for l in subprocess.run([str(exe)], input="\n".join(lines) + "\n", capture_output=True, text=True).stdout.strip().split("\n") if l]
def scenario(exe):
    ok = {}
    t = [f"S 1", "T 0 1 %d" % HZ] + [f"T {HZ * s} 0 {HZ}" for s in range(1, 62)]
    r = run(exe, t)
    first = next((i for i, x in enumerate(r) if x[0] == 1), None)
    ok["1 min: asleep after 60 idle seconds, not before"] = first is not None and first == 60                       # r[0] is the press; r[k] is second k
    r = run(exe, ["S 1", "T 0 1 %d" % HZ] + [f"T {HZ * s} 0 {HZ}" for s in range(1, 40)] + [f"T {HZ * 40} 1 {HZ}"] + [f"T {HZ * s} 0 {HZ}" for s in range(41, 106)])
    ok["a press restarts the count"] = next((k for k, x in enumerate(r) if x[0] == 1), None) == 100 and [x[0] for x in r[:100]].count(1) == 0
    r = run(exe, ["S 1", "T 0 1 %d" % HZ] + [f"T {HZ * s} 0 {HZ}" for s in range(1, 62)] + [f"T {HZ * 62} 1 {HZ}", f"T {HZ * 63} 0 {HZ}"])
    ok["the waking press is reported (2) and the screen stays awake after it"] = r[-2][0] == 2 and r[-2][1] == 0 and r[-1][0] == 0
    r = run(exe, ["S 0", "T 0 1 %d" % HZ] + [f"T {HZ * s} 0 {HZ}" for s in range(1, 4000, 7)])
    ok["off never sleeps"] = all(x[0] == 0 for x in r)
    base = 0xFFFFFF00
    r = run(exe, ["S 1", f"T {base} 1 {HZ}"] + [f"T {(base + HZ * s) & 0xFFFFFFFF} 0 {HZ}" for s in range(1, 62)])
    ok["the count survives the cycle counter wrapping"] = any(x[0] == 1 for x in r) and next(i for i, x in enumerate(r) if x[0] == 1) == 60
    return ok
with tempfile.TemporaryDirectory() as td:
    for k, v in scenario(build(td, 0)).items(): check(k, v)
    b = build(td, 1); check("mutant (a second counts as a minute) is caught", not all(scenario(b).values()))
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
