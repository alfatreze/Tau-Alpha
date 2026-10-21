#!/usr/bin/env python3
"""Waterfall grid picture checks (fw2/ui/meter_waterfall_grid.c, real code under P2_PACK) on a framebuffer model: the grid is always visible (an unlit cell is a dim cell, never the background, in every mode,
and the spectrogram keeps it while it scrolls); the scroll speed is a pixel speed (equal for 5 px and 12 px cells, 450 px in 5 s at 90 px/s); ZONES: a loud band lights only its own 4x4 region and fills it from
the centre outwards. Mutants: unlit cells drawn as background; the speed counted per row instead of per pixel; zones scatter."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
def run(mutant):
    with tempfile.TemporaryDirectory() as td:
        d = Path(td); (d / "fw2/ui").mkdir(parents=True); (d / "fw2/gfx").mkdir(); (d / "fw2/cores").mkdir(); (d / "fw2/generated").mkdir(); (d / "sim").mkdir()
        src = (ROOT / "fw2/ui/meter_waterfall_grid.c").read_text()
        if mutant == "nogrid": a = "pal[4] = { rc_ramp(bg, UI_ACCENT, 1u, 7u),"; assert a in src; src = src.replace(a, "pal[4] = { bg,")
        if mutant == "perrow": a = "acc_q8 += (uint32_t)in->dt_ms * cfg.speed * 256u / 1000u;"; assert a in src; src = src.replace(a, "acc_q8 += (uint32_t)in->dt_ms * cfg.speed * 256u / 1000u * pitch / 9u;")
        if mutant == "scatter": a = "if (cfg.mode == 1u) v = cell_step(spec[region_band(c, r, cols, rows)], region_thr(c, r, cols, rows));"; assert a in src; src = src.replace(a, "if (0) v = 0;")
        (d / "fw2/ui/meter_waterfall_grid.c").write_text(src)
        for f in ("ui/meter.h", "ui/theme.h", "gfx/gfx.h", "generated/meter_presets.h", "cores/ramp_core.h", "cores/meter_core.h"): (d / "fw2" / f).write_text((ROOT / "fw2" / f).read_text())
        (d / "sim/h.c").write_text((ROOT / "sim/p2_waterfall_harness.c").read_text())
        r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True)
        if r.returncode: print(r.stderr); sys.exit(1)
        return subprocess.run([str(d / "h")], capture_output=True, text=True).stdout.strip().splitlines()
good = run(None)
print("\n".join(good))
fails = 0 if good[-1] == "PASSED" else 1
for m, name in (("nogrid", "unlit cells are the background"), ("perrow", "the speed depends on the cell size"), ("scatter", "zones are not structured")):
    out = run(m); ok = out[-1] == "FAILED"; print(("ok   " if ok else "FAIL ") + "mutant (%s) is caught" % name); fails += 0 if ok else 1
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
