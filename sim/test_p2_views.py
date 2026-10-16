#!/usr/bin/env python3
"""View core (fw2/cores/view_core.h): the one place a view is left. Logs: 10/11/12/13 = view 0 enter/exit/input/draw, 20/21/22 = view 1, 30/31 = view 2."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(extra)))
    if not ok: fails += 1
def build(td, bug):
    exe = Path(td) / f"v{bug}"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_views_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, text): return subprocess.run([str(exe)], input=text, capture_output=True, text=True).stdout.strip()
with tempfile.TemporaryDirectory() as td:
    g = build(td, 0)
    check("start enters the first view and marks its regions", run(g, "") == "10 | cur=0 prev=0 switches=0 dirty=100", run(g, ""))
    o = run(g, "F\nS 1\n")
    check("a switch leaves the old view, enters the new one, then marks the new view's regions dirty", o == "10 11 20 | cur=1 prev=0 switches=1 dirty=011", o)
    o = run(g, "S 1\nS 1\n")
    check("switching to the view that is already current does nothing", o == "10 11 20 | cur=1 prev=0 switches=1 dirty=011", o)
    o = run(g, "S 1\n")
    check("what the old view still had pending is forgotten on a switch (it would draw over the new view)", o == "10 11 20 | cur=1 prev=0 switches=1 dirty=011", o)
    o = run(g, "K 1\n")
    check("a view's input can ask for another view", o == "10 12 11 20 | cur=1 prev=0 switches=1 dirty=011", o)
    o = run(g, "S 1\nK 2\n")
    check("VW_BACK returns to the view before", o == "10 11 20 22 21 10 | cur=0 prev=1 switches=2 dirty=100", o)
    o = run(g, "K 7\n")
    check("an input that asks for nothing changes nothing", o == "10 12 | cur=0 prev=0 switches=0 dirty=100", o)
    o = run(g, "S 2\n")
    check("a switch asked for from inside an enter is applied after it, never nested", o == "10 11 30 31 10 | cur=0 prev=2 switches=2 dirty=100", o)
    o = run(g, "S 5\nS 200\n")
    check("an unknown view is ignored", o == "10 | cur=0 prev=0 switches=0 dirty=100", o)
    o = run(g, "D\nS 1\nD\n")
    check("draw goes to the current view only (view 1 has none)", o == "10 13 11 20 | cur=1 prev=0 switches=1 dirty=011", o)
    o = run(g, "F\nR\n")
    check("a refresh repaints the current view in place (its redraw, else its enter) and marks its regions, without exit/enter", o == "10 10 | cur=0 prev=0 switches=0 dirty=100", o)
    o = run(g, "S 1\nF\nR\n")
    check("a view with its own redraw uses it", o == "10 11 20 24 | cur=1 prev=0 switches=1 dirty=011", o)
    o = run(g, "H\nS 1\n")
    check("with frame hooks a switch is drawn inside the frame: begin, exit, enter, (regions marked), end", o == "10 90 11 20 92 | cur=1 prev=0 switches=1 dirty=011", o)
    o = run(g, "H\nR\n")
    check("a refresh is inside a frame too", o == "10 90 10 92 | cur=0 prev=0 switches=0 dirty=100", o)
    o = run(g, "H\nS 2\n")
    check("a switch asked from inside an enter makes its own frame after the first", o == "10 90 11 30 91 90 31 10 92 | cur=0 prev=2 switches=2 dirty=100", o)
    o = run(build(td, 5), "H\nS 1\n")
    check("mutant 5 (the frame ends before the regions are marked) is caught", o != "10 90 11 20 92 | cur=1 prev=0 switches=1 dirty=011", o)
    for bug, what in ((1, "the old view's exit is never called"), (2, "the new view's regions are not marked dirty"), (3, "the old view's pending regions survive the switch")):
        m = build(td, bug)
        caught = run(m, "S 1\n") != "10 11 20 | cur=1 prev=0 switches=1 dirty=011"
        check(f"mutant {bug} ({what}) is caught", caught)
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
