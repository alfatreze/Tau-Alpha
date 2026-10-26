#!/usr/bin/env python3
"""Help pages and fallback classification (fw2/cores/help_core.h): Up/Down turn the page and wrap, B or Left closes, no line is longer than the 36 characters that fit the widest font, the Tau Omega address
is on page 1, and the fallback screen is chosen from the library state: missing file, unreadable (any other error code), empty, tracks that cannot be opened; nothing while the library is not yet loaded or fine."""
import re, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(extra)))
    if not ok: fails += 1
def build(td, bug):
    exe = Path(td) / f"h{bug}"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_help_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, keys): return subprocess.run([str(exe)], input=keys, capture_output=True, text=True).stdout.strip().split("\n")
with tempfile.TemporaryDirectory() as td:
    g = build(td, 0)
    o = run(g, ""); m = re.match(r"open page=0 pages=(\d+)", o[0]); n = int(m[1])
    check("Help has three pages and opens on the first", n == 3, o[0])
    o = run(g, "U"); check("Up from the first page wraps to the last", o[1] == "U page=2 close=0", o[1])
    o = run(g, "D D D"); check("Down three times wraps back to the first page", o[3] == "D page=0 close=0", o[3])
    o = run(g, "D L"); check("Left closes Help", o[2].endswith("close=1"), o)
    o = run(g, "D B"); check("B closes Help", o[2].endswith("close=1"), o)
    o = run(g, "k 2 10 0 0 k 2 13 0 0 k 2 128 0 0 k 1 0 0 0 k 1 0 5 0 k 1 0 5 1 k 0 0 0 0")
    kinds = [int(x.split()[1]) for x in o if x.startswith("kind")]
    check("no file (10) = NOLIB 1, other errors (13, 128) = UNREADABLE 2, empty = EMPTY 3, tracks that cannot be opened = NOFILES 4, a good or not yet loaded library = nothing", kinds == [1, 2, 2, 3, 0, 4, 0], kinds)
    check("no help or fallback line is longer than 36 characters, and page 1 carries the Tau Omega address", o[-1] == "long=0 omega_url_on_help=1", o[-1])
    for bug, what, scen in ((1, "paging clamps instead of wrapping", "U"), (2, "a missing file is shown as damaged", "k 2 10 0 0")):
        b = build(td, bug); check(f"mutant caught: {what}", run(b, scen) != run(g, scen))
print("FAILED" if fails else "PASSED"); sys.exit(1 if fails else 0)
