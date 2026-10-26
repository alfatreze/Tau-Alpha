#!/usr/bin/env python3
"""The Waterfall's wide colour maps (fw2/ui/meter_waterfall_grid.c wf_col, wf_palette, the 32-level CLUT of the spectrogram): the colour of every level of INFERNO, RAINBOW and ICE equals an independent
interpolation of the same stops (within one unit per RGB565 channel); end to end on the engine model (sim/p2_waterfall_equiv_harness.c -DPALETTE_TEST) a constant level paints brighter and brighter grids as the
level rises (INFERNO and ICE: mean luma strictly increasing), the wide maps show many more colours than THEME's three steps, THEME is unchanged. Mutants: a map interpolates to the wrong stop, the CLUT keeps the
first map."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ((" " + str(extra)[:300]) if (extra and not ok) else ""))
    if not ok: fails += 1
STOPS = {1: [(0, 0, 4), (40, 11, 84), (101, 21, 110), (159, 42, 99), (212, 72, 66), (245, 125, 21), (250, 193, 39), (252, 255, 164)],
         2: [(0, 0, 96), (0, 0, 255), (0, 160, 255), (0, 255, 160), (160, 255, 0), (255, 160, 0), (255, 0, 0)],
         3: [(0, 0, 8), (10, 30, 110), (20, 100, 200), (110, 200, 240), (255, 255, 255)]}
def expect(map_, v):
    st = STOPS[map_]; n = len(st); p = v * (n - 1); seg = min(p // 255, n - 2); f = p - seg * 255
    return tuple((st[seg][k] * (255 - f) + st[seg + 1][k] * f) / 255 for k in range(3))
def build(d, mutant):
    (d / "fw2/ui").mkdir(parents=True); (d / "fw2/gfx").mkdir(); (d / "fw2/cores").mkdir(); (d / "fw2/generated").mkdir(); (d / "sim").mkdir()
    src = (ROOT / "fw2/ui/meter_waterfall_grid.c").read_text()
    if mutant == 1: a = "static const uint8_t off[4] = { 0, 0, 24, 45 }"; assert a in src; src = src.replace(a, "static const uint8_t off[4] = { 0, 0, 21, 45 }")
    if mutant == 2: a = "const uint32_t sig = ((uint32_t)cfg.palette << 16) | bg;"; assert a in src; src = src.replace(a, "const uint32_t sig = bg;")
    (d / "fw2/ui/wf.c").write_text(src)
    for f in ("ui/meter.h", "ui/theme.h", "gfx/gfx.h", "generated/meter_presets.h", "cores/ramp_core.h", "cores/meter_core.h", "ui/meter_grid.h", "cores/cellgrid_core.h", "cores/gfx_touch_core.h"): (d / "fw2" / f).write_text((ROOT / "fw2" / f).read_text())
    (d / "sim/h.c").write_text((ROOT / "sim/p2_waterfall_equiv_harness.c").read_text())
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DPALETTE_TEST", '-DWF_SRC="../fw2/ui/wf.c"', "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return d / "h"
def run(exe): return subprocess.run([str(exe)], capture_output=True, text=True).stdout.splitlines()
def parse(out):
    cols, s = {}, {}
    for l in out:
        p = l.split()
        if p[0] == "C": cols[(int(p[1]), int(p[2]))] = int(p[3])
        elif p[0] == "S": s[(int(p[1]), int(p[2]))] = (int(p[3]), int(p[4]))
    return cols, s
def verdicts(out):
    cols, s = parse(out); ok = {}
    bad = []
    for (m, v), c in cols.items():
        e = expect(m, v); r5, g6, b5 = (c >> 11) & 31, (c >> 5) & 63, c & 31
        if abs(r5 - int(e[0]) // 8) > 1 or abs(g6 - int(e[1]) // 4) > 1 or abs(b5 - int(e[2]) // 8) > 1: bad.append((m, v, c))
    ok["the colour of every level of the three maps equals the independent interpolation"] = not bad and len(cols) == 768
    levels = (0, 51, 102, 153, 204, 255)
    for m, name in ((1, "INFERNO"), (3, "ICE")):
        lumas = [s[(m, lv)][1] for lv in levels]
        ok["%s: a louder constant level paints a brighter grid (mean luma %s)" % (name, lumas)] = all(a < b for a, b in zip(lumas, lumas[1:]))
    ok["RAINBOW: the louder, the further along the map (the grid colour changes with every level)"] = len({s[(2, lv)] for lv in levels}) == len(levels)
    ok["a wide map shows many colours across a spectrum (a gradient across the bands: INFERNO %d, RAINBOW %d, ICE %d) and THEME's four steps only %d" % (s[(1, 306)][0], s[(2, 306)][0], s[(3, 306)][0], s[(0, 306)][0])] = all(s[(m, 306)][0] >= 12 for m in (1, 2, 3)) and s[(0, 306)][0] <= 5
    ok["the three maps paint different grids at the same level (the CLUT follows the map when it changes)"] = all(len({s[(m, lv)] for m in (1, 2, 3)}) == 3 for lv in (102, 153, 204))
    ok["silence paints only the dim grid in every map"] = all(s[(m, 0)][0] <= 2 for m in range(4))
    return ok
with tempfile.TemporaryDirectory() as td:
    d = Path(td)
    res = verdicts(run(build(d / "a", 0)))
    for k, v in res.items(): check(k, v)
    for m, name in ((1, "a map interpolates to the wrong stop"), (2, "the CLUT keeps the first map when the map changes")):
        r = verdicts(run(build(d / ("m%d" % m), m))); check("mutant (%s) is caught" % name, not all(r.values()))
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
