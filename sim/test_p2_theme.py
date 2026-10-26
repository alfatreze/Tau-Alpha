#!/usr/bin/env python3
"""Theme arithmetic (fw2/cores/theme_core.h): the accent on a light theme (scaled down to luma 110, hue kept) for every one of the 19 palette colours and for random 16-bit colours, against an independent
restatement; the colour pick wraps; an invalid saved index falls back."""
import random, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(extra)))
    if not ok: fails += 1
def build(td, bug):
    exe = Path(td) / f"t{bug}"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_theme_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def ask(exe, text): return subprocess.run([str(exe)], input=text, capture_output=True, text=True).stdout.split()
def model(a, light):
    if not light: return a
    r, g, b = (a >> 11 & 31) * 255 // 31, (a >> 5 & 63) * 255 // 63, (a & 31) * 255 // 31
    l = (2126 * r + 7152 * g + 722 * b) // 10000
    if l > 110:
        r, g, b = r * 110 // l, g * 110 // l, b * 110 // l
        a = (((r * 31 + 127) // 255) << 11) | (((g * 63 + 127) // 255) << 5) | ((b * 31 + 127) // 255)
    return a
PAL = [0x18C3, 0xF7BE, 0x2A83, 0xE73B, 0x8C51, 0xD1E6, 0xE429, 0x5D0D, 0x4BD4, 0x9BF6, 0xFEA0, 0xE429, 0xD1E6, 0xE4B6, 0x4BD4, 0x5D0D, 0x6AD2, 0xBDF7, 0x8C71]
rnd = random.Random(5); colours = PAL + [rnd.randrange(65536) for _ in range(500)]
with tempfile.TemporaryDirectory() as td:
    g = build(td, 0)
    q = "".join(f"A {c} {p}\n" for c in colours for p in (0, 1))
    out = list(map(int, ask(g, q)))
    want = [model(c, p) for c in colours for p in (0, 1)]
    check("accent on dark and light equals the independent model (19 palette colours + 500 random)", out == want, next((i for i, (a, b) in enumerate(zip(out, want)) if a != b), -1))
    check("a light theme never shows an accent brighter than luma 110 + rounding", all(((x >> 11 & 31) * 255 // 31 * 2126 + (x >> 5 & 63) * 255 // 63 * 7152 + (x & 31) * 255 // 31 * 722) // 10000 <= 114 for x in out[1::2]))
    check("the colour pick wraps both ways", ask(g, "P 18 1\nP 0 -1\nP 5 1\nP 5 -1\n") == ["0", "18", "6", "4"])
    check("an invalid saved index falls back to the default", ask(g, "V 3 19 1\nV 25 19 1\nV 31 2 0\n") == ["3", "1", "0"])
    for bug, what, probe in ((1, "bright accents survive on light", "A 35953 1\nA 35953 0\n"), (2, "the pick clamps instead of wrapping", "P 18 1\nP 0 -1\n")):
        check(f"mutant {bug} ({what}) is caught", ask(build(td, bug), probe) != ask(g, probe))
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
