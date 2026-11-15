#!/usr/bin/env python3
"""Settings pages (fw2/cores/menupages_core.h): the previous player's menu structure as data and the navigation as logic.
Pages: 0 Home, 1 Appearance, 2 Audio, 3 Playback, 4 Diagnostics, 5 option list. Effects: 0 none, 1 set value, 2 action, 3 close. Value ids: 0 volume 1 replaygain 2 time stretch 3 repeat 4 shuffle 5 seek step 6 skip silence 7 speed 8 album art 9 accept all rates."""
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
    check("Home opens on the first row; Up wraps to the last (INFO), Down wraps back", head.endswith("page=0 sel=0") and r[0][3] == 7 and r[1][3] == 0 and r[-1][3] == 5 and r[3][3] == 2, r[:3])
    _, r = run(g, "R B B")
    check("Right opens a group, B goes back, B at Home closes Settings", r[0][1:4] == (1, 1, 0) and r[1][1:4] == (0, 0, 0) and r[2][5] == 3, r)
    _, r = run(g, "R D D D D D A L")
    check("a toggle (ALBUM ART): A flips it, Left flips it back (Left is not 'back' on a toggle)", r[6][5:] == (1, 8, 1) and r[7][5:] == (1, 8, 0) and r[7][1] == 1, r[5:])
    _, r = run(g, "R D D D D A R L")
    check("METER: CONFIGURE is an action: A reports ACT_CONFIGURE (4), Right does nothing, Left is back", r[5][5:7] == (2, 4) and r[6][5] == 0 and r[6][1] == 1 and r[7][1] == 0, r)
    _, r = run(g, "D R v 0 100 R L L v 0 0 L")
    check("volume: Right at 100 changes nothing, Left steps down by 3 (not back), Left at 0 changes nothing and stays", r[2][5] == 0 and r[3][5:] == (1, 0, 97) and r[3][1] == 1 and r[4][5:] == (1, 0, 94) and r[5][5] == 0 and r[5][1] == 1, r)
    _, r = run(g, "D R D v 1 2 A U A")
    check("a choice (REPLAYGAIN): A opens its list on the current value, A on another option sets it and returns to the row", r[3][1:4] == (2, 5, 2) and r[4][3] == 1 and r[5][5:] == (1, 1, 1) and r[5][1:4] == (1, 2, 1), r)
    _, r = run(g, "D R D A D B")
    check("B in an option list backs out without a change", r[5][5] == 0 and r[5][1:4] == (1, 2, 1), r)
    _, r = run(g, "D D R D D A D D A")
    check("TIME STRETCH (Playback, third row) lists three modes and sets the chosen one", r[5][1:4] == (2, 5, 0) and r[8][5:] == (1, 2, 2), r)
    _, r = run(g, "D D R D D D A")
    check("SPEED (Playback, fourth row) cannot be opened while the mode is Normal (one option)", r[6][1:4] == (1, 3, 3) and r[6][5] == 0, r)
    _, r = run(g, "D D D R A D A D D A D A")
    check("Diagnostics: CHECK is an action (5), CYMO RESAMPLER (15) toggles, ACCEPT ALL RATES (9) toggles, REPORT is an action (2)", r[4][5:] == (2, 5, 0) and r[6][5:] == (1, 15, 1) and r[9][5:] == (1, 9, 1) and r[11][5:] == (2, 2, 0), r)
    _, r = run(g, "U U U U A")
    check("CONTROLS (fourth from last row of Home) is an action (ACT_CONTROLS 6)", r[4][5:] == (2, 6, 0), r)
    _, r = run(g, "U U U A")
    check("HELP (third from last row of Home) is an action (ACT_HELP 7)", r[3][5:] == (2, 7, 0), r)
    _, r = run(g, "U U A")
    check("ABOUT (second to last row of Home) is an action (ACT_ABOUT 8)", r[2][5:] == (2, 8, 0), r)
    _, r = run(g, "U A")
    check("INFO (last row of Home) is an action", r[1][5:] == (2, 1, 0), r)
    _, r = run(g, "D D R D A D D D D D D D D D D A")
    check("Playback: SHUFFLE (second row) toggles", r[4][5:] == (1, 4, 1), r)
    # ---------- the METER list scrolls through up to 64 rows (P2.5): 7 visible, the selection always inside them, Up/Down wrap, it opens on the current meter
    def listwalk(n, cur, keys):
        _, r = run(g, "n %d v 13 %d R D D D R %s" % (n, cur, keys))
        return r
    for n in (7, 8, 63, 64):
        for cur in (0, n // 2, n - 1):
            r = listwalk(n, cur, "")
            vis_ok = r[-1][1:3] == (2, 5) and r[-1][3] == cur and r[-1][4] <= cur < r[-1][4] + 7 and r[-1][4] <= max(0, n - 7)
            check("METER list over %d rows opens on the current meter %d, visible (top %d), highlighted" % (n, cur, r[-1][4]), vis_ok, r[-1])
        r = listwalk(n, 0, " ".join("D" for _ in range(n + 2)))
        sels = [x[3] for x in r[5:]]; tops = [x[4] for x in r[5:]]
        check("%d rows: Down visits every row in order and WRAPS to row 0 (the menu convention), the selection is always one of the 7 visible rows" % n,
              sels[:n] == [(i + 1) % n for i in range(n)] and all(t <= s < t + 7 for s, t in zip(sels, tops)) and all(t <= max(0, n - 7) for t in tops), (sels[:12], tops[:12]))
        r = listwalk(n, 0, " ".join("U" for _ in range(3)))
        sels = [x[3] for x in r[5:]]
        check("%d rows: Up from row 0 wraps to row %d" % (n, n - 1), sels[0] == n - 1 and r[5][4] == max(0, n - 7) and all(t <= s < t + 7 for s, t in zip(sels, [x[4] for x in r[5:]])), r[5:])
    r = listwalk(64, 9, "D D D D")
    check("scrolling is by one row: the window follows the selection down (row 9 opens showing rows 3..9, then each Down moves it by one: 4, 5, 6, 7)", [x[4] for x in r[5:]] == [4, 5, 6, 7], [x[4] for x in r[5:]])
    _, r = run(g, "n 64 v 13 40 R D D D R A")
    check("A on the highlighted row picks it: the value id of METER (13) and the row position 40, back to Appearance", r[-1][5:] == (1, 13, 40) and r[-1][1] == 1, r[-1])
    _, r = run(g, "n 64 v 13 40 R D D D R B")
    check("B backs out of the list without a change", r[-1][5] == 0 and r[-1][1] == 1, r[-1])
    m4 = build(td, 4)
    check("mutant 4 (the METER list scrolls for 8 rows instead of 7: its last row is off screen) is caught", run(m4, "n 64 v 13 0 R D D D R D D D D D D D") != run(g, "n 64 v 13 0 R D D D R D D D D D D D"))
    for bug, what, scen in ((1, "Left on the volume goes back", "D R v 0 0 L"), (2, "an option sets the wrong value id", "D R D A D A"), (3, "backing out of an option list applies it", "D R D A D B")):
        m = build(td, bug)
        check(f"mutant {bug} ({what}) is caught", run(m, scen) != run(g, scen))
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
