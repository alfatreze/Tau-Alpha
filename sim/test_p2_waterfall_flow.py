#!/usr/bin/env python3
"""The Waterfall's horizontal spectrogram (fw2/ui/meter_waterfall_grid.c, flow 1, gfx_scroll_left): on the engine model (sim/p2_waterfall_equiv_harness.c -DFLOW_TEST) the LOW bands paint the bottom of the box and
the HIGH bands the top (frequency up the side), a one-tick pulse then moves LEFT at the scroll speed (mean x of its bright pixels falls by speed x time, within 25 %), the right edge is empty again after it has
passed, and nothing is drawn outside the box. Mutants: the picture scrolls the wrong way (right); frequency upside down."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ((" " + str(extra)[:300]) if (extra and not ok) else ""))
    if not ok: fails += 1
def build(d, mutant):
    (d / "fw2/ui").mkdir(parents=True); (d / "fw2/gfx").mkdir(); (d / "fw2/cores").mkdir(); (d / "fw2/generated").mkdir(); (d / "sim").mkdir()
    src = (ROOT / "fw2/ui/meter_waterfall_grid.c").read_text()
    if mutant == 1: a = "if (gw > d) gfx_scroll_left(ox, oy, gw, rows * pitch, d);"; assert a in src; src = src.replace(a, "if (gw > d) gfx_scroll_down(ox, oy, gw, rows * pitch, d);")
    if mutant == 2: a = "col_level(spec, rows - 1u - r, rows)"; assert a in src; src = src.replace(a, "col_level(spec, r, rows)")
    (d / "fw2/ui/wf.c").write_text(src)
    for f in ("ui/meter.h", "ui/theme.h", "gfx/gfx.h", "generated/meter_presets.h", "cores/ramp_core.h", "cores/meter_core.h", "ui/meter_grid.h", "cores/cellgrid_core.h", "cores/gfx_touch_core.h"): (d / "fw2" / f).write_text((ROOT / "fw2" / f).read_text())
    (d / "sim/h.c").write_text((ROOT / "sim/p2_waterfall_equiv_harness.c").read_text())
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DFLOW_TEST", '-DWF_SRC="../fw2/ui/wf.c"', "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return d / "h"
def verdicts(exe):
    out = subprocess.run([str(exe)], capture_output=True, text=True).stdout.splitlines(); ok = {}
    B = {int(l.split()[1]): (int(l.split()[2]), int(l.split()[3])) for l in out if l.startswith("B ")}
    P = [(int(l.split()[1]), int(l.split()[2]) / 10.0, int(l.split()[3])) for l in out if l.startswith("P ")]
    R = next(int(l.split()[1]) for l in out if l.startswith("R ")); O = next(int(l.split()[1]) for l in out if l.startswith("O "))
    ok["low frequencies paint the bottom, high frequencies the top (mean row of band 0: %s, band 15: %s of 1000)" % (B[0][0], B[15][0])] = B[0][1] > 50 and B[15][1] > 50 and B[0][0] > 700 and B[15][0] < 300
    seen = [p for p in P if p[2] > 20]
    ok["a one-tick pulse enters at the right and moves LEFT (%d ticks followed)" % len(seen)] = len(seen) >= 12 and all(b[1] < a[1] for a, b in zip(seen[2:], seen[3:]))
    if len(seen) >= 12:
        t0, x0 = seen[3][0], seen[3][1]; t1, x1 = seen[-1][0], seen[-1][1]; speed = (x0 - x1) / ((t1 - t0) * 0.040)
        ok["it moves at the scroll speed: %.0f px/s against 120 px/s" % speed] = 90 <= speed <= 150
    else: ok["it moves at the scroll speed"] = False
    ok["after the pulse has passed the right edge is empty again (%d bright pixels in the last 8 columns)" % R] = R == 0
    ok["nothing is drawn outside the box (%d pixels)" % O] = O == 0
    return ok
with tempfile.TemporaryDirectory() as td:
    d = Path(td)
    for k, v in verdicts(build(d / "a", 0)).items(): check(k, v)
    for m, name in ((1, "the picture scrolls the wrong way"), (2, "frequency upside down")):
        r = verdicts(build(d / ("m%d" % m), m)); check("mutant (%s) is caught" % name, not all(r.values()))
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
