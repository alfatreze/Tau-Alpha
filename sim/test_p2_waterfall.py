#!/usr/bin/env python3
"""Waterfall grid picture checks (fw2/ui/meter_waterfall_grid.c, real code under P2_PACK) on a framebuffer model: the grid is always visible (an unlit cell is a dim cell, never the background, in every mode,
and the spectrogram keeps it while it scrolls); the scroll speed is a pixel speed (equal for 5 px and 12 px cells, 450 px in 5 s at 90 px/s); ZONES: a loud band lights only its own 4x4 region and fills it from
the centre outwards. SPARKLE: silence lights nothing, a band's jump flashes cells around its own column, a sustained tone is one burst, flashes fade to the dim grid. Mutants: unlit cells drawn as background; every loud picture an onset; flashes never fade; flashes ignore the band; the speed counted per row instead of per pixel; zones scatter."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
def run(mutant):
    with tempfile.TemporaryDirectory() as td:
        d = Path(td); (d / "fw2/ui").mkdir(parents=True); (d / "fw2/gfx").mkdir(); (d / "fw2/cores").mkdir(); (d / "fw2/generated").mkdir(); (d / "sim").mkdir()
        src = (ROOT / "fw2/ui/meter_waterfall_grid.c").read_text()
        if mutant == "nogrid": a = "pal[4] = { rc_ramp(bg, UI_ACCENT, 1u, 7u),"; assert a in src; src = src.replace(a, "pal[4] = { bg,")
        if mutant == "perrow": a = "acc_q8 += (uint32_t)in->dt_ms * cfg.speed * 256u / 1000u;"; assert a in src; src = src.replace(a, "acc_q8 += (uint32_t)in->dt_ms * cfg.speed * 256u / 1000u * pitch / 9u;")
        if mutant == "sparkle_all": a = "if (lv < av + (WF_ONSET << 4) || fresh >= WF_MAX_NEW) continue;"; assert a in src; src = src.replace(a, "if (lv < 16u) continue;")
        if mutant == "sparkle_stay": a = "h = zget(i), nh = h > dec ? h - dec : 0u,"; assert a in src; src = src.replace(a, "h = zget(i), nh = h,")
        if mutant == "sparkle_col": a = "centre = b * (cols - 1u) / (MTR_BANDS - 1u);"; assert a in src; src = src.replace(a, "centre = 0u;")
        if mutant == "caps": a = "#define WF_COLS 67u"; assert a in src; src = src.replace(a, "#define WF_COLS 48u").replace("#define WF_ROWS 54u", "#define WF_ROWS 40u")
        if mutant == "scatter": a = "q = (uint32_t)spec[band] >> 4;"; assert a in src; src = src.replace(a, "q = (uint32_t)spec[(band * 7u + 3u) & 15u] >> 4;")
        (d / "fw2/ui/meter_waterfall_grid.c").write_text(src)
        for f in ("ui/meter.h", "ui/theme.h", "gfx/gfx.h", "generated/meter_presets.h", "cores/ramp_core.h", "cores/meter_core.h"): (d / "fw2" / f).write_text((ROOT / "fw2" / f).read_text())
        (d / "sim/h.c").write_text((ROOT / "sim/p2_waterfall_harness.c").read_text())
        r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True)
        if r.returncode: print(r.stderr); sys.exit(1)
        return subprocess.run([str(d / "h")], capture_output=True, text=True).stdout.strip().splitlines()
good = run(None)
print("\n".join(good))
fails = 0 if good[-1] == "PASSED" else 1
for m, name in (("nogrid", "unlit cells are the background"), ("perrow", "the speed depends on the cell size"), ("scatter", "zones are not structured"), ("sparkle_all", "every loud picture is an onset"), ("sparkle_stay", "flashes never fade"), ("sparkle_col", "flashes ignore the band"), ("caps", "the grid is capped at 48 x 40 cells")):
    out = run(m); ok = out[-1] == "FAILED"; print(("ok   " if ok else "FAIL ") + "mutant (%s) is caught" % name); fails += 0 if ok else 1
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
