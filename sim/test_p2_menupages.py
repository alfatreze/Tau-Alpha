#!/usr/bin/env python3
"""Settings pages (fw2/cores/menupages_core.h): the previous player's menu structure as data and the navigation as logic.
Pages: 0 Home, 1 Appearance, 2 Audio, 3 Playback, 4 Diagnostics, 5 option list. Effects: 0 none, 1 set value, 2 action, 3 close. Value ids: 0 volume 1 replaygain 2 tempo 3 repeat 4 shuffle 5 seek step 6 skip silence 7 album art 8 accept all rates."""
import re, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(extra)))
    if not ok: fails += 1
def build(td, bug):
    exe = Path(td) / f"m{bug}"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-unused-variable", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_menupages_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, keys):
    o = subprocess.run([str(exe)], input=keys, capture_output=True, text=True).stdout.strip().split("\n")
    res = []
    for l in o[1:]:
        m = re.match(r"(\S+) depth=(\d+) page=(\d+) sel=(\d+) top=(\d+) eff=(\d+),(\d+),(\d+)", l)
        res.append(tuple(int(x) if x.isdigit() else x for x in m.groups()))
    return o[0], res                                        # (key, depth, page, sel, top, effect kind, id, value)
with tempfile.TemporaryDirectory() as td:
    g = build(td, 0)
    head, r = run(g, "U D D D D D D")
    check("Home opens on the first row; Up wraps to the last (INFO), Down wraps back", head.endswith("page=0 sel=0") and r[0][3] == 4 and r[1][3] == 0 and r[-1][3] == 0 and r[3][3] == 2, r[:3])
    _, r = run(g, "R B B")
    check("Right opens a group, B goes back, B at Home closes Settings", r[0][1:4] == (1, 1, 0) and r[1][1:4] == (0, 0, 0) and r[2][5] == 3, r)
    _, r = run(g, "R D D D D D A L")
    check("a toggle (ALBUM ART): A flips it, Left flips it back (Left is not 'back' on a toggle)", r[6][5:] == (1, 7, 1) and r[7][5:] == (1, 7, 0) and r[7][1] == 1, r[5:])
    _, r = run(g, "R D D D D A R L")
    check("a placeholder row (METER: CONFIGURE) does nothing on A or Right; Left is back", r[5][5] == 0 and r[5][1] == 1 and r[6][5] == 0 and r[6][1] == 1 and r[7][1] == 0, r)
    _, r = run(g, "D R v 0 100 R L L v 0 0 L")
    check("volume: Right at 100 changes nothing, Left steps down by 3 (not back), Left at 0 changes nothing and stays", r[2][5] == 0 and r[3][5:] == (1, 0, 97) and r[3][1] == 1 and r[4][5:] == (1, 0, 94) and r[5][5] == 0 and r[5][1] == 1, r)
    _, r = run(g, "D R D v 1 2 A U A")
    check("a choice (REPLAYGAIN): A opens its list on the current value, A on another option sets it and returns to the row", r[3][1:4] == (2, 5, 2) and r[4][3] == 1 and r[5][5:] == (1, 1, 1) and r[5][1:4] == (1, 2, 1), r)
    _, r = run(g, "D R D A D B")
    check("B in an option list backs out without a change", r[5][5] == 0 and r[5][1:4] == (1, 2, 1), r)
    _, r = run(g, "D D R D D A D D A")
    check("TEMPO (Playback, third row) lists eight options and sets the chosen one", r[5][1:4] == (2, 5, 0) and r[8][5:] == (1, 2, 2), r)
    _, r = run(g, "D D D R A D A")
    check("Diagnostics: ACCEPT ALL RATES toggles; REPORT is an action", r[3][1:3] == (1, 4) and r[4][5:] == (1, 8, 1) and r[6][5:] == (2, 2, 0), r)
    _, r = run(g, "U A")
    check("INFO (last row of Home) is an action", r[1][5:] == (2, 1, 0), r)
    _, r = run(g, "D D R D A D D D D D D D D D D A")
    check("Playback: SHUFFLE (second row) toggles", r[4][5:] == (1, 4, 1), r)
    for bug, what, scen in ((1, "Left on the volume goes back", "D R v 0 0 L"), (2, "an option sets the wrong value id", "D R D A D A"), (3, "backing out of an option list applies it", "D R D A D B")):
        m = build(td, bug)
        check(f"mutant {bug} ({what}) is caught", run(m, scen) != run(g, scen))
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
