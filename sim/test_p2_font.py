#!/usr/bin/env python3
"""Font loader core (fw2/cores/font_core.h) against the real records of tools/gen_font_atlases.py and a mock engine: a record parses only when whole and signed; a bad one (flipped bit) is refused before anything is
written; the load fences the engine first, restarts the index, writes all 6,112 words once, and demands the load counter stepped by exactly one; with no font memory nothing is written; the advances replace the
output table only on success. Mutants: no CRC check, no fence, no counter check."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(extra)))
    if not ok: fails += 1
def build(td, bug):
    exe = Path(td) / f"f{bug}"
    r = subprocess.run(["cc", "-O1", "-Wall", "-Werror", "-Wno-unused-function", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_font_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, text): return subprocess.run([str(exe)], input=text, capture_output=True, text=True, cwd=ROOT).stdout.strip().split("\n")        # cwd = the repo: the file names below are relative (the repo path has spaces)
SB = "fw2/generated/fonts/SpaceGrotesk-SemiBold.tfnt"; TZ = "fw2/generated/fonts/Tamzen8x16r.tfnt"
def scen(exe):
    ok = {}
    r = run(exe, f"R {SB}\nL 1 1\n")
    ok["a real record parses and loads: 6,112 words, the advances copied"] = r[0] == "parse 0" and r[1].startswith("load 0 words 6112 adv0 ") and r[1].split()[5] != "238"
    ok["the engine is fenced, then the index restarted, then the first word (in that order)"] = r[1].split(" log ")[1].split()[:3] == ["fence", "ctrl1", "firstw"]
    r = run(exe, f"R {TZ}\nL 1 1\n")
    ok["Tamzen loads too and its first advance is 8"] = r[0] == "parse 0" and r[1].split()[5] == "8"
    ok["a flipped bit in the record makes it unparseable"] = run(exe, f"R {SB}\nF 100\n")[-1] == "parse 1"
    r = run(exe, f"R {SB}\nL 0 1\n")
    ok["no font memory in the bitstream: nothing is written, the advances stay untouched (0xEE)"] = r[1].startswith("load 2 words 0 adv0 238")
    r = run(exe, f"R {SB}\nL 1 2\n")
    ok["a load counter that moved by two is an error and the advances stay untouched"] = r[1].startswith("load 3 words 6112 adv0 238")
    r = run(exe, f"R {SB}\nL 1 0\n")
    ok["a load counter that did not move is an error"] = r[1].startswith("load 3 ")
    return ok
with tempfile.TemporaryDirectory() as td:
    g = scen(build(td, 0))
    for k, v in g.items(): check(k, v)
    for bug, what in ((1, "no CRC check"), (2, "no fence"), (3, "no load-counter check")):
        m = scen(build(td, bug)); check(f"mutant {bug} ({what}) is caught", not all(m.values()))
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
