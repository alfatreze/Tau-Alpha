#!/usr/bin/env python3
"""Tests for tools/derive_text_weights.py (the glyph coverage -> blend weight table, derived from first principles)."""
import os, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import derive_text_weights as d

fails = 0
def check(name, ok, extra=""):
    global fails
    print(("PASS " if ok else "FAIL ") + name + ("" if ok else "  " + str(extra)))
    fails += 0 if ok else 1

dark = d.theme_pairs("dark"); light = d.theme_pairs("light")
check("the themes give (text, ground) pairs for both polarities", len(dark) > 10 and len(light) > 10, (len(dark), len(light)))
td, _ = d.derive(dark); tl, _ = d.derive(light)
for name, t in (("dark", td), ("light", tl)):
    check(f"{name}: 16 entries in 0..16, untouched pixel is the background (0) and a solid one the foreground (16)",
          len(t) == 16 and t[0] == 0 and t[15] == 16 and all(0 <= w <= 16 for w in t), t)
    check(f"{name}: the table never decreases", all(t[i] <= t[i + 1] for i in range(15)), t)
check("deterministic: the same inputs give the same table", d.derive(dark)[0] == td)
# physics: light text on a dark ground needs MORE weight than plain linear alpha at mid coverage, dark on light needs LESS
lin = d.linear_table()
check("light-on-dark mid coverage is heavier than linear alpha", td[7] > lin[7] and td[8] > lin[8], (td, lin))
check("dark-on-light mid coverage is lighter than linear alpha", tl[7] < lin[7] and tl[8] < lin[8], (tl, lin))
# brute force on one pair: the chosen weight is the minimiser of the squared L* error
fg, bg = (255, 255, 255), (0, 0, 0)
t1, _ = d.derive([(fg, bg, 1.0)])
ok = True
for c in range(1, 15):
    a = c / 15
    best = min(range(17), key=lambda w: d.pair_error(bg, fg, a, w))
    ok &= (t1[c] == best) or (t1[c] == t1[c - 1] > best)          # allowed to differ only where monotonicity lifted it
check("one pair: every entry is the brute-force minimiser (up to the non-decreasing rule)", ok, t1)
# the fit is better than plain linear alpha on the real palette
md, _ = d.report(td, dark); ml, _ = d.report(lin, dark)
check("derived table has a lower mean L* error than linear alpha on the dark palette", md < ml, (md, ml))
# colour helpers
check("565 round trip of white and black", d.unpack565(d.pack565((255, 255, 255))) == (255, 255, 255) and d.unpack565(0) == (0, 0, 0))
check("blend with weight 0 is the background and weight 16 the foreground", d.blend565((10, 20, 30), (200, 100, 50), 0) == d.unpack565(d.pack565((10, 20, 30))) and d.blend565((10, 20, 30), (200, 100, 50), 16) == d.unpack565(d.pack565((200, 100, 50))))
print("derive_text_weights test:", "all passed" if not fails else f"{fails} FAILED")
sys.exit(1 if fails else 0)
