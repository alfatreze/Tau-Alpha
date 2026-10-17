#!/usr/bin/env python3
"""Player 2 icon drawing (fw2/gfx/icon.c, fw2/cores/icon_core.h) against the draw-engine model: after an icon is drawn every pixel equals the colour table entry of its coverage (the table is the background blended toward the
foreground, loaded at every draw); another icon or other colours in between leave nothing stale; every icon of the record draws; an unknown id is refused; with double buffering the icon lands in the buffer being drawn
into. Mutant: the colour steps collapsed to two (the expected colours are computed by a second, independent formula in the harness)."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    if not ok: fails += 1
def build(td, bug):
    exe = Path(td) / f"i{bug}"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DP2_HOST", f"-DBUG={bug}", "-include", str(ROOT / "sim/p2_host_regs.h"), "-o", str(exe), str(ROOT / "sim/p2_icon_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, scen=""): return subprocess.run([str(exe), scen], capture_output=True, text=True).stdout.strip().split("\n")
with tempfile.TemporaryDirectory() as td:
    exe = build(td, 0)
    for scen in ("flat", "dbuf"):
        out = run(exe, scen)
        for l in out:
            if l.startswith("ok  ") or l.startswith("FAIL "): check(l[5:], l.startswith("ok"))
        check(f"the {scen} scenario finished cleanly", out[-1] == "PASSED")
    m2 = run(build(td, 2), "flat")
    check("mutant (the colour steps collapsed to two) is caught", m2[-1] != "PASSED")
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
