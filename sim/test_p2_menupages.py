#!/usr/bin/env python3
"""Settings pages (fw2/cores/menupages_core.h): the previous player's menu structure as data and the navigation as logic.
Pages: 0 Home, 1 Appearance, 2 Audio, 3 Playback, 4 Diagnostics, 5 option list. W1 of release 0.7.5 (D-M25): METERS is its own row of Home, directly below APPEARANCE, and opens the METER list; Appearance has no METER and no METER: CONFIGURE row. Effects: 0 none, 1 set value, 2 action, 3 close. Value ids: 0 volume 1 replaygain 2 time stretch 3 repeat 4 shuffle 5 seek step 6 skip silence 7 speed 8 album art 9 accept all rates."""
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
def rows(exe, pg):
    o = [l for l in subprocess.run([str(exe)], input="P %d" % pg, capture_output=True, text=True).stdout.split("\n") if l.startswith("page ")][0]
    body = o.split(" rows ", 1)[1]
    return [tuple(x.strip().rsplit("|", 2)) for x in body.split(";") if x.strip()]          # (label, kind, arg)
def run(exe, keys):
    o = subprocess.run([str(exe)], input=keys, capture_output=True, text=True).stdout.strip().split("\n")
    o = [l for l in o if not l.startswith("page ")]
    res = []
    for l in o[1:]:
        m = re.match(r"(\S+) depth=(\d+) page=(\d+) sel=(\d+) top=(\d+) eff=(\d+),(\d+),(\d+)", l)
        res.append(tuple(int(x) if x.isdigit() else x for x in m.groups()))
    return o[0], res                                        # (key, depth, page, sel, top, effect kind, id, value)
with tempfile.TemporaryDirectory() as td:
    g = build(td, 0)
    HOME = [r[0] for r in rows(g, 0)]; APP = [r[0] for r in rows(g, 1)]
    def to(label, page=HOME): return " ".join(["D"] * page.index(label) + ["R"])             # from the top of the page: the keys that open `label`
    def down(label, page): return " ".join(["D"] * page.index(label))
    nh = len(HOME)
    check("Home: APPEARANCE first and METERS DIRECTLY BELOW it (D-M25), then AUDIO and PLAYBACK; CONTROLS HELP ABOUT (INFO last in the Diagnostic Build)", HOME[:4] == ["APPEARANCE", "METERS", "AUDIO", "PLAYBACK"] and HOME[-4:] == ["CONTROLS", "HELP", "ABOUT", "INFO"], HOME)
    check("METERS is a CHOICE row of the METER value (it opens the list), nothing else on Home changed kind", rows(g, 0)[1] == ("METERS", "1", "13") and rows(g, 0)[0] == ("APPEARANCE", "0", "1"), rows(g, 0)[:2])
    check("Appearance has COLOUR THEME MODE ALBUM ART SCREEN BLANK FONT and no METER and no METER: CONFIGURE row", APP == ["COLOUR", "THEME", "MODE", "ALBUM ART", "SCREEN BLANK", "FONT"], APP)
    head, r = run(g, "U D D D D D D")
    check("Home opens on the first row; Up wraps to the last (INFO), Down wraps back", head.endswith("page=0 sel=0") and r[0][3] == nh - 1 and r[1][3] == 0 and r[-1][3] == 5 and r[3][3] == 2, r[:3])
    _, r = run(g, "R B B")
    check("Right opens a group, B goes back, B at Home closes Settings", r[0][1:4] == (1, 1, 0) and r[1][1:4] == (0, 0, 0) and r[2][5] == 3, r)
    k = "R " + down("ALBUM ART", APP) + " A L"; _, r = run(g, k); n = len(k.split())
    check("a toggle (ALBUM ART): A flips it, Left flips it back (Left is not 'back' on a toggle)", r[n - 2][5:] == (1, 8, 1) and r[n - 1][5:] == (1, 8, 0) and r[n - 1][1] == 1, r[n - 3:])
    _, r = run(g, to("AUDIO") + " v 0 100 R L L v 0 0 L")
    nk = 1 + HOME.index("AUDIO")
    check("volume: Right at 100 changes nothing, Left steps down by 3 (not back), Left at 0 changes nothing and stays", r[nk][5] == 0 and r[nk + 1][5:] == (1, 0, 97) and r[nk + 1][1] == 1 and r[nk + 2][5:] == (1, 0, 94) and r[nk + 3][5] == 0 and r[nk + 3][1] == 1, r)
    _, r = run(g, to("AUDIO") + " D v 1 2 A U A")
    nk = 1 + HOME.index("AUDIO")
    check("a choice (REPLAYGAIN): A opens its list on the current value, A on another option sets it and returns to the row", r[nk + 1][1:4] == (2, 5, 2) and r[nk + 2][3] == 1 and r[nk + 3][5:] == (1, 1, 1) and r[nk + 3][1:4] == (1, 2, 1), r)
    _, r = run(g, to("AUDIO") + " D A D B")
    nk = 1 + HOME.index("AUDIO")
    check("B in an option list backs out without a change", r[nk + 3][5] == 0 and r[nk + 3][1:4] == (1, 2, 1), r)
    P = [r[0] for r in rows(g, 3)]
    _, r = run(g, to("PLAYBACK") + " " + down("TIME STRETCH (MP3)", P) + " A D D A")
    nk = 1 + HOME.index("PLAYBACK") + P.index("TIME STRETCH (MP3)")
    check("TIME STRETCH (Playback) lists three modes and sets the chosen one", r[nk][1:4] == (2, 5, 0) and r[nk + 3][5:] == (1, 2, 2), r)
    _, r = run(g, to("PLAYBACK") + " " + down("SPEED (MP3)", P) + " A")
    check("SPEED (Playback) cannot be opened while the mode is Normal (one option)", r[-1][1] == 1 and r[-1][5] == 0, r)
    DG = [r[0] for r in rows(g, 4)]
    _, r = run(g, to("DIAGNOSTICS") + " A D A " + " ".join(["D"] * (DG.index("ACCEPT ALL RATES") - 1)) + " A D A")
    nk = 1 + HOME.index("DIAGNOSTICS")
    check("Diagnostics: CHECK is an action (5), CYMO RESAMPLER (15) toggles, ACCEPT ALL RATES (9) toggles, REPORT is an action (2)", r[nk][5:] == (2, 5, 0) and r[nk + 2][5:] == (1, 15, 1) and r[nk + 5][5:] == (1, 9, 1) and r[nk + 7][5:] == (2, 2, 0), r)
    _, r = run(g, "U U U U A")
    check("CONTROLS (fourth from last row of Home) is an action (ACT_CONTROLS 6)", r[4][5:] == (2, 6, 0), r)
    _, r = run(g, "U U U A")
    check("HELP (third from last row of Home) is an action (ACT_HELP 7)", r[3][5:] == (2, 7, 0), r)
    _, r = run(g, "U U A")
    check("ABOUT (second to last row of Home) is an action (ACT_ABOUT 8)", r[2][5:] == (2, 8, 0), r)
    _, r = run(g, "U A")
    check("INFO (last row of Home) is an action", r[1][5:] == (2, 1, 0), r)
    _, r = run(g, to("PLAYBACK") + " D A")
    nk = 1 + HOME.index("PLAYBACK")
    check("Playback: SHUFFLE (second row) toggles", r[nk + 1][5:] == (1, 4, 1), r)
    # ---------- METERS (the METER list as its own entry of Home)
    NK = HOME.index("METERS") + 1                                                          # keys that open the list: Down to METERS, Right
    for n1 in (1, 2):
        _, r = run(g, "n %d v 13 0 " % n1 + to("METERS"))
        check("METERS opens the list even over %d row(s) (Configure of the only meter must stay reachable); depth 1, the option-list page, on the first row" % n1, r[-1][1:4] == (1, 5, 0), r[-1])
    _, r = run(g, "n 9 v 13 6 " + to("METERS"))
    check("METERS opens on the ACTIVE meter (value 6 of 9), visible", r[-1][1:4] == (1, 5, 6) and r[-1][4] <= 6 < r[-1][4] + 7, r[-1])
    _, r = run(g, "n 9 v 13 6 " + to("METERS") + " B")
    check("B in the METERS list goes back to Home, on the METERS row, no effect", r[-1][1:4] == (0, 0, 1) and r[-1][5] == 0, r[-1])
    _, r = run(g, "n 9 v 13 6 " + to("METERS") + " L")
    check("Left in the METERS list goes back to Home", r[-1][1:4] == (0, 0, 1), r[-1])
    _, r = run(g, "n 9 v 13 6 " + to("METERS") + " B B")
    check("B on Home closes Settings (the list is one level deep)", r[-1][5] == 3, r[-1])
    # ---------- the METER list scrolls through up to 64 rows (P2.5): 7 visible, the selection always inside them, Up/Down wrap, it opens on the current meter
    def listwalk(n, cur, keys):
        _, r = run(g, "n %d v 13 %d %s %s" % (n, cur, to("METERS"), keys))
        return r
    for n in (7, 8, 63, 64):
        for cur in (0, n // 2, n - 1):
            r = listwalk(n, cur, "")
            vis_ok = r[-1][1:3] == (1, 5) and r[-1][3] == cur and r[-1][4] <= cur < r[-1][4] + 7 and r[-1][4] <= max(0, n - 7)
            check("METER list over %d rows opens on the current meter %d, visible (top %d), highlighted" % (n, cur, r[-1][4]), vis_ok, r[-1])
        r = listwalk(n, 0, " ".join("D" for _ in range(n + 2)))
        sels = [x[3] for x in r[NK:]]; tops = [x[4] for x in r[NK:]]
        check("%d rows: Down visits every row in order and WRAPS to row 0 (the menu convention), the selection is always one of the 7 visible rows" % n,
              sels[:n] == [(i + 1) % n for i in range(n)] and all(t <= s < t + 7 for s, t in zip(sels, tops)) and all(t <= max(0, n - 7) for t in tops), (sels[:12], tops[:12]))
        r = listwalk(n, 0, " ".join("U" for _ in range(3)))
        sels = [x[3] for x in r[NK:]]
        check("%d rows: Up from row 0 wraps to row %d" % (n, n - 1), sels[0] == n - 1 and r[NK][4] == max(0, n - 7) and all(t <= s < t + 7 for s, t in zip(sels, [x[4] for x in r[NK:]])), r[NK:])
    r = listwalk(64, 9, "D D D D")
    check("scrolling is by one row: the window follows the selection down (row 9 opens showing rows 3..9, then each Down moves it by one: 4, 5, 6, 7)", [x[4] for x in r[NK:]] == [4, 5, 6, 7], [x[4] for x in r[NK:]])
    _, r = run(g, "n 64 v 13 40 " + to("METERS") + " A")
    check("A on the highlighted row picks it: the value id of METER (13) and the row position 40, back to Home", r[-1][5:] == (1, 13, 40) and r[-1][1] == 0, r[-1])
    _, r = run(g, "n 64 v 13 40 " + to("METERS") + " B")
    check("B backs out of the list without a change", r[-1][5] == 0 and r[-1][1] == 0, r[-1])
    m4 = build(td, 4)
    sc4 = "n 64 v 13 0 " + to("METERS") + " D D D D D D D"
    check("mutant 4 (the METER list scrolls for 8 rows instead of 7: its last row is off screen) is caught", run(m4, sc4) != run(g, sc4))
    AU = to("AUDIO")
    for bug, what, scen in ((1, "Left on the volume goes back", AU + " v 0 0 L"), (2, "an option sets the wrong value id", AU + " D A D A"), (3, "backing out of an option list applies it", AU + " D A D B")):
        m = build(td, bug)
        check(f"mutant {bug} ({what}) is caught", run(m, scen) != run(g, scen))
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
