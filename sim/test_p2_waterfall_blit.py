#!/usr/bin/env python3
"""The Waterfall's entering row as a palette blit (B-802): the spectrogram drawn with the entering-row plane in the stash and gfx_cblit (fw2/ui/meter_waterfall_grid.c) must leave exactly the picture the rectangle version
left (sim/p2_waterfall_ref.c.txt, a frozen copy of the code before the change), after every one of 2,400 ticks over six scenarios (cells 5 to 12 px, speeds 20 to 320 px/s, two box sizes, silence, pauses, forced
repaints over stale pixels), on a model of the engine's rectangle fill, stash rectangle, scroll, palette load and palette blit (a blit wider than 127 columns or outside the frame fails). It must also issue fewer commands.
Mutants: the blit reads the plane one row too low; the plane's background index is the dim step."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    if not ok: fails += 1
def run(src_text, tag):
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        for sub in ("fw2/ui", "fw2/gfx", "fw2/cores", "fw2/generated", "sim"): (d / sub).mkdir(parents=True)
        (d / "fw2/ui/wf.c").write_text(src_text)
        for f in ("ui/meter.h", "ui/theme.h", "gfx/gfx.h", "generated/meter_presets.h", "cores/ramp_core.h", "cores/meter_core.h", "cores/gfx_touch_core.h"): (d / "fw2" / f).write_text((ROOT / "fw2" / f).read_text())
        (d / "sim/h.c").write_text((ROOT / "sim/p2_waterfall_equiv_harness.c").read_text())
        r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", '-DWF_SRC="../fw2/ui/wf.c"', "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True)
        if r.returncode: print(r.stderr); sys.exit(1)
        return subprocess.run([str(d / "h")], capture_output=True, text=True).stdout.strip().splitlines()
new_src = (ROOT / "fw2/ui/meter_waterfall_grid.c").read_text()
ref = run((ROOT / "sim/p2_waterfall_ref.c.txt").read_text(), "ref")
got = run(new_src, "new")
check("2,400 ticks produced", len(ref) == 2401 and len(got) == 2401 and not any(l.startswith("FAIL") for l in got))
check("the box holds the same picture as the rectangle version after every tick", ref[:-1] == got[:-1])
rc, gc = int(ref[-1].split()[1]), int(got[-1].split()[1])
print("     commands (model units): rectangles %d, palette blit %d" % (rc, gc))
check("the palette blit issues fewer commands", gc < rc)
for name, a, b in (("the blit reads the plane one row too low", "plane_blit(in, gw, pitch - expo, 0u, expo)", "plane_blit(in, gw, pitch - expo + 1u, 0u, expo)"),
                   ("the background index is the dim step", "gfx_rect_stash(0u, WF_PLANE_Y, gw, pitch, 4u)", "gfx_rect_stash(0u, WF_PLANE_Y, gw, pitch, 0u)")):
    assert a in new_src, a
    check("mutant (%s) is caught" % name, run(new_src.replace(a, b, 1), "mut")[:-1] != ref[:-1])
print("PASSED" if not fails else "FAILED (%d)" % fails)
sys.exit(1 if fails else 0)
