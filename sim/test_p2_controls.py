#!/usr/bin/env python3
"""Settings > CONTROLS (fw2/cores/controls_core.h): a read-only list of every button with icons. Up/Down move and wrap, the page scrolls to keep the highlight in view, B or Left goes back, the volume
speaker follows the five ranges of the icon sheet, and every icon id is a real icon; also that every key listed is one the firmware really handles (checked against main_player.c / nowplaying.c)."""
import re, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(extra)))
    if not ok: fails += 1
def build(td, bug):
    exe = Path(td) / f"c{bug}"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_controls_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, keys):
    return subprocess.run([str(exe)], input=keys, capture_output=True, text=True).stdout.strip().split("\n")
with tempfile.TemporaryDirectory() as td:
    g = build(td, 0)
    o = run(g, "")
    m = re.match(r"open sel=0 top=0 n=(\d+) vis=(\d+)", o[0]); n, vis = int(m[1]), int(m[2])
    check("the page opens on the first row and has more rows than fit (it scrolls)", n > vis, o[0])
    o = run(g, "U")
    check("Up from the first row wraps to the last and scrolls to the end", o[1] == f"U sel={n-1} top={n-vis} back=0", o[1])
    o = run(g, " ".join("D" * n))
    check("Down n times wraps back to the first row at the top", o[-2] == "D sel=0 top=0 back=0", o[-2])
    o = run(g, " ".join("D" * vis))
    check("the highlight scrolls the page one row when it leaves the bottom", o[vis] == f"D sel={vis} top=1 back=0", o[vis])
    o = run(g, "D B"); check("B goes back", o[2].endswith("back=1"), o)
    o = run(g, "L"); check("Left goes back", o[1].endswith("back=1"), o)
    o = run(g, "v 0 v 1 v 33 v 34 v 66 v 67 v 99 v 100")
    ic = [int(x.split("icon=")[1]) for x in o[1:9]]
    check("volume speaker: 0 mute, 1-33 / 34-66 / 67-99 the three speakers, 100 the full one (the five ranges of the sheet)", ic == [9, 10, 10, 11, 11, 12, 12, 13], ic)
    check("every icon id used by a row is a real icon", o[-1] == "icons: ok", o[-1])
    for bug, what, scen in ((1, "Up/Down clamp instead of wrapping", "U"), (2, "the volume ranges are off by one at their ends", "v 33 v 66 v 99")):
        b = build(td, bug)
        check(f"mutant caught: {what}", run(b, scen) != run(g, scen))
src = (ROOT / "fw2/app/main_player.c").read_text() + (ROOT / "fw2/ui/nowplaying.c").read_text()
for key, pat in (("A play/pause", "KEY_A) mp3play_act(MPA_PLAYPAUSE"), ("B restart", "KEY_B) mp3play_act(MPA_RESTART"), ("X meter", "KEY_X) meters_next"), ("Y EQ", "KEY_Y) halcyon_cycle"),
                 ("L accent", "KEY_L1) theme_pick_step"), ("Select+L repeat", "KEY_L1) mp3play_act(MPA_REPEAT"), ("Select+X preset", "KEY_X) meters_preset_next"), ("Select+Y fullscreen", "KEY_Y) views_switch(cur == V_FULLSCREEN"),
                 ("Select+Up album", "KEY_UP) mp3play_act(MPA_ALBUM"), ("Select+Left seek 1 s", "KEY_LEFT) mp3play_act(MPA_SEEK, -1)")):
    check(f"the firmware handles: {key}", pat in src, pat)
print("FAILED" if fails else "PASSED"); sys.exit(1 if fails else 0)
