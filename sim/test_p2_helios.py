#!/usr/bin/env python3
"""Helios core (fw2/cores/helios_core.h): beam-aware drawing, dirty regions, the wrap rule.
The beam oracle is a small physical model, not the formula: the scanout reads row y one line before it is shown, one row every 8 write-units; the draw engine starts writing after a latency of 3 row-times and writes one row per unit. A span is
SAFE when every row is read either entirely before or entirely after it is written (no row is torn, and the frame does not mix old and new rows). hl_rows_safe must never call an unsafe span safe, and must not be needlessly timid."""
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
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_helios_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def ask(exe, text): return subprocess.run([str(exe)], input=text, capture_output=True, text=True).stdout.split("\n")
VOFF, VACT = 4, 360
def oracle_one(vc, y0, y1, latency):
    """Drawing rows [y0, y1), the engine finishing row y at unit latency + (y - y0); the beam shows row `by` now and reads row by+k during units [(k-1)*8, k*8)."""
    if vc < VOFF or vc >= VOFF + VACT: return True
    by = vc - VOFF
    states = set()
    for y in range(y0, y1):
        if y <= by: states.add("old"); continue              # already scanned this frame: old content now, the new one next frame
        k = y - by
        w = latency + (y - y0)
        if w < (k - 1) * 8: states.add("new")                 # written before the read starts
        elif w >= k * 8: states.add("old")                    # written after the read finished
        else: return False                                    # written while the row is being read: a torn row
    return not ("old" in states and "new" in states)          # a frame that mixes rows is a visible tear
def oracle_safe(vc, y0, y1):
    """Safe whatever the engine's start-up latency is (a short one, 3 units, or three row-times, 24)."""
    return all(oracle_one(vc, y0, y1, L) for L in (3, 24))
spans = [(0, 20), (0, 100), (50, 120), (200, 300), (212, 292), (296, 340), (300, 360), (340, 360), (0, 360), (10, 11), (100, 101)]
cases = [(vc, a, b) for a, b in spans for vc in range(0, 400)]
with tempfile.TemporaryDirectory() as td:
    good = build(td, 0)
    q = "".join(f"S {vc} {a} {b}\n" for vc, a, b in cases)
    out = ask(good, q)[:-1]
    cases = [(vc, a, b) for a, b in spans for vc in range(0, 400)]
    unsound = [(c, o) for c, o in zip(cases, out) if o == "1" and not oracle_safe(*c)]
    check("never calls a span that would tear safe (every beam line, 11 spans)", not unsound, unsound[:3])
    timid = [c for c, o in zip(cases, out) if o == "0" and oracle_safe(*c) and c[0] >= VOFF and c[0] < VOFF + VACT and (c[1] - (c[0] - VOFF) > 12 or c[2] < c[0] - VOFF - 3)]
    check("not timid: a span far ahead of the beam or well behind it is drawn", not timid, timid[:3])
    check("in vertical blanking everything is safe", all(o == "1" for (vc, a, b), o in zip(cases, out) if vc < VOFF or vc >= VOFF + VACT))
    check("without a beam counter everything is safe", set(ask(good, "".join(f"N {a} {b} {vc}\n" for a, b in spans for vc in range(0, 400, 7)))[:-1]) == {"1"})
    w = ask(good, "W 0 5 1\nW 4 5 0\nW 2 5 1\nW 2 5 0\nW 0 1 1\nW 0 1 0\nW 0 0 1\n")[:-1]
    check("wrap: Up on the first row goes to the last, Down on the last to the first, one row stays", w == ["4", "0", "1", "3", "0", "0", "0"], w)
    t = ask(good, "R\n")
    check("regions: ids, nothing drawn when clean", t[0] == "ids 0 1 2" and t[1].startswith("clean n=0 []"), t[:2])
    check("regions: the one the beam is inside waits, the others draw (whole-screen at once)", t[2].startswith("beam in region 1 (row 250) n=2 [0,2]") and "deferred=1" in t[2], t[2])
    check("regions: the deferred one draws when the beam has passed", t[3].startswith("beam moved past region 1 n=1 [1]"), t[3])
    check("regions: nothing left afterwards; a mask marks several", t[4].startswith("again n=0") and t[5].startswith("mask 0b101 in blanking n=2 [0,2]"), t[4:6])
    check("regions: a full table refuses with 0xFF, never a crash", t[6] == "full 255", t[6])
    for bug, what in ((1, "passed span judged safe one row too early"), (2, "no lead ahead of the beam"), (3, "wrap clamps"), (4, "regions ignore the beam")):
        m = build(td, bug)
        o = ask(m, q)[:-1]; wm = ask(m, "W 0 5 1\nW 4 5 0\n")[:-1]; tm = ask(m, "R\n")
        caught = (any(x == "1" and not oracle_safe(*c) for c, x in zip(cases, o)) or wm != ["4", "0"] or tm != t)
        check(f"mutant {bug} ({what}) is caught", caught)
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
