#!/usr/bin/env python3
"""Colour picker core (fw2/cores/colour_core.h): channels R (0..31), G (0..63), B (0..31); Up/Down choose the channel and wrap, Left/Right step by one and clamp, A accepts, B restores the original.
Mutant: a channel wraps instead of clamping."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    if not ok: fails += 1
UP, DOWN, LEFT, RIGHT, A, B = 1, 2, 4, 8, 16, 32
def build(td, bug):
    exe = Path(td) / f"p{bug}"
    r = subprocess.run(["cc", "-O1", "-Wall", "-Werror", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_colour_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, text): return [tuple(int(x) for x in l.split()) for l in subprocess.run([str(exe)], input=text, capture_output=True, text=True).stdout.strip().split("\n") if l]
def scenario(exe):
    ok = {}
    r = run(exe, f"O 0\nK {RIGHT}\nK {RIGHT}\n")
    ok["Right raises red by one step each press (red is the top five bits)"] = r[0] == (1, 2048, 0) and r[1] == (1, 4096, 0)
    r = run(exe, f"O 0\nK {DOWN}\nK {RIGHT}\nK {DOWN}\nK {RIGHT}\n")
    ok["Down moves to green then blue; each channel has its own bits"] = r[1] == (1, 32, 1) and r[3] == (1, 33, 2)
    r = run(exe, f"O 63488\nK {RIGHT}\nK {LEFT}\n")
    ok["a channel clamps at its maximum (red 31)"] = r[0] == (0, 63488, 0) and r[1] == (1, 61440, 0)
    r = run(exe, f"O 0\nK {LEFT}\n")
    ok["a channel clamps at zero"] = r[0] == (0, 0, 0)
    r = run(exe, f"O 2016\nK {DOWN}\nK {RIGHT}\n")
    ok["green has six bits (63 is its maximum)"] = r[1] == (0, 2016, 1)
    r = run(exe, f"O 0\nK {UP}\n")
    ok["Up from red wraps to blue"] = r[0] == (0, 0, 2)
    r = run(exe, f"O 1234\nK {RIGHT}\nK {A}\nK {B}\n")
    ok["A accepts the edited value; B restores the original"] = r[1][0] == 2 and r[1][1] != 1234 and r[2] == (3, 1234, 0)
    return ok
with tempfile.TemporaryDirectory() as td:
    g = scenario(build(td, 0))
    for k, v in g.items(): check(k, v)
    m = scenario(build(td, 1))
    check("mutant (a channel wraps) is caught", any(not v for v in m.values()))
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
