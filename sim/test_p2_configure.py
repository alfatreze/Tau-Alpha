#!/usr/bin/env python3
"""Meter Configure page core (fw2/cores/configure_core.h): hidden parameters and unmet `when` conditions are not listed; a number steps by its step and clamps, a choice wraps, an on/off toggles; Up/Down wrap and the window follows;
turning a switch off removes the rows that depend on it (the selection stays put); PRESET and RESET report their effect; B closes. Mutant: a number wraps instead of clamping."""
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
    exe = Path(td) / f"c{bug}"
    r = subprocess.run(["cc", "-O1", "-Wall", "-Werror", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_configure_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
# parameters: (type hidden min max step nval when_idx when_val)
PARAMS = "P 6\n0 0 4 16 1 0 255 0\n1 0 0 1 1 4 255 0\n2 0 0 1 1 0 255 0\n0 0 0 800 20 0 2 1\n0 1 0 10 1 0 255 0\n0 0 1 100 5 0 255 0\n"
def run(exe, text): return subprocess.run([str(exe)], input=text, capture_output=True, text=True).stdout.strip().split("\n")
def scenario(exe):
    ok = {}
    r = run(exe, PARAMS + "V 16 0 1 100 3 50\nD\n")
    ok["rows: preset, the visible parameters (hidden one skipped), reset"] = r[0].split("|")[1].split() == ["254", "0", "1", "2", "3", "5", "253"]
    r = run(exe, PARAMS + "V 16 0 0 100 3 50\nD\n")
    ok["a parameter whose condition is not met is not listed"] = r[0].split("|")[1].split() == ["254", "0", "1", "2", "5", "253"]
    r = run(exe, PARAMS + f"V 16 0 1 100 3 50\nK {DOWN}\nK {RIGHT}\nD\n")                                # select param 0 (4..16 step 1), already at max: clamps
    ok["a number clamps at its maximum"] = r[-1].split("|")[2].split()[0] == "16"
    r = run(exe, PARAMS + f"V 5 0 1 100 3 50\nK {DOWN}\nK {LEFT}\nK {LEFT}\nD\n")
    ok["a number clamps at its minimum"] = r[-1].split("|")[2].split()[0] == "4"
    r = run(exe, PARAMS + f"V 16 0 1 100 3 50\nK {DOWN}\nK {DOWN}\nK {LEFT}\nD\n")                       # param 1 is a 4-way choice at 0: Left wraps to 3
    ok["a choice wraps"] = r[-1].split("|")[2].split()[1] == "3"
    r = run(exe, PARAMS + f"V 16 0 1 100 3 50\nK {DOWN}\nK {DOWN}\nK {DOWN}\nK {RIGHT}\nD\n")            # param 2 is on/off: becomes 0
    ok["an on/off toggles"] = r[-1].split("|")[2].split()[2] == "0"
    r = run(exe, PARAMS + f"V 16 0 1 100 3 50\nK {DOWN}\nK {DOWN}\nK {DOWN}\nK {RIGHT}\nD\n")
    d = r[-1]
    ok["after the switch goes off the rows depending on it are gone and the selection is on the switch"] = d.split("|")[1].split() == ["254", "0", "1", "2", "5", "253"] and d.startswith("rows 6 sel 3")
    r = run(exe, PARAMS + f"V 16 0 1 100 3 50\nK {UP}\nD\n")
    ok["Up from the first row wraps to the last (reset)"] = r[-1].startswith("rows 7 sel 6")
    r = run(exe, PARAMS + f"V 16 0 1 100 3 50\nK {RIGHT}\nK {A}\nK {UP}\nK {A}\nK {B}\n")
    ok["preset row reports a step, A on reset reports reset, B closes"] = r == ["eff 2 1", "eff 0 0", "eff 0 0", "eff 3 0", "eff 4 0"]
    return ok
with tempfile.TemporaryDirectory() as td:
    for k, v in scenario(build(td, 0)).items(): check(k, v)
    b = build(td, 1); check("mutant (a number wraps instead of clamping) is caught", not all(scenario(b).values()))
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
