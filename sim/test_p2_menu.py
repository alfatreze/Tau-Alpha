#!/usr/bin/env python3
"""Player 2 settings menu logic (fw2/cores/menu_core.h): the C core equals an independent model over random key scripts (selection wraps, the window follows the selection, a range clamps and
reports no change at its end, a choice wraps); 2 mutants must be caught."""
import random, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ((" " + str(extra)) if (extra and not ok) else ""))
    if not ok: fails += 1
ITEMS = [(0, 0, 100, 1), (1, 0, 2, 1), (1, 0, 7, 1), (0, 0, 100, 10), (1, 0, 1, 1), (0, 5, 60, 5)]
class M:
    def __init__(s, rows): s.rows = rows; s.sel = 0; s.top = 0; s.v = [94, 0, 2, 50, 0, 30]
    def fix(s):
        if s.sel < s.top: s.top = s.sel
        if s.sel >= s.top + s.rows: s.top = s.sel + 1 - s.rows
        s.top = min(s.top, max(6 - s.rows, 0))
    def move(s, d): s.sel = (0 if s.sel + 1 >= 6 else s.sel + 1) if d > 0 else (5 if s.sel == 0 else s.sel - 1); s.fix()
    def adjust(s, d):
        k, lo, hi, st = ITEMS[s.sel]; v = s.v[s.sel]
        if k == 1: nv = (lo if v + 1 > hi else v + 1) if d > 0 else (hi if v <= lo else v - 1)
        else: nv = min(v + st, hi) if d > 0 else (lo if v < lo + st else v - st)
        if nv == v: return 0
        s.v[s.sel] = nv; return 1
def build(d, bug):
    exe = d / f"m{bug}"
    r = subprocess.run(["cc", "-O1", "-Wall", "-Werror", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_menu_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def agrees(exe):
    for sd in range(250):
        r = random.Random(sd); rows = r.choice([1, 3, 4, 6]); cmds = [f"init {rows}"] + [r.choice(["up", "down", "left", "right", "right", "left"]) for _ in range(r.randrange(10, 150))]
        got = subprocess.run([str(exe)], input="\n".join(cmds) + "\n", capture_output=True, text=True).stdout.strip().split("\n")
        m = M(rows)
        for c, g in zip(cmds[1:], got[1:]):
            ch = 0
            if c == "up": m.move(-1)
            elif c == "down": m.move(1)
            elif c == "left": ch = m.adjust(-1)
            else: ch = m.adjust(1)
            f = list(map(int, g.split()))
            if f[0] != m.sel or f[1] != m.top or f[2] != ch or f[3:] != m.v: return False
            if not (m.top <= m.sel < m.top + rows): return False
    return True
with tempfile.TemporaryDirectory() as td:
    d = Path(td)
    check("C core equals the independent model over 250 random key scripts (window invariant holds)", agrees(build(d, 0)))
    for bug, name in ((1, "range not clamped"), (2, "change reported when none happened")):
        check(f"mutant {bug} ({name}) is caught", not agrees(build(d, bug)))
print("FAILED" if fails else "all ok", fails); sys.exit(1 if fails else 0)
