#!/usr/bin/env python3
"""fw2/ui/meter.c drawn picture == a clean render of its own bar/cap state after every window (sim/p2_meter_harness.c), for loud/quiet bursts where bars fall while their caps are held.
The mutant restores the old erase (it only cleared the strip between the old and new tops, so a bar that shrank under a held cap stayed full height until the cap fell)."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    if not ok: fails += 1
def build(td, mutant):
    src = (ROOT / "fw2/ui/meter.c").read_text()
    if mutant:
        new = "        if (old_top + 2u > px[b]) gfx_rect(x, base - old_top - 2u, BAR_W, old_top + 2u - px[b], BG);"
        assert new in src
        src = src.replace(new, "        { uint32_t new_top = px[b] > cap_px[b] ? px[b] : cap_px[b]; if (old_top > new_top) gfx_rect(x, base - old_top - 2u, BAR_W, old_top - new_top + 3u, BG); }")
    d = Path(td); (d / "ui").mkdir(); (d / "platform").mkdir(); (d / "cores").mkdir(); (d / "gfx").mkdir(); (d / "sim").mkdir()
    (d / "ui/meter.c").write_text(src)
    for f in ("ui/meter.h",): (d / f).write_text((ROOT / "fw2" / f).read_text())
    for f in ("platform/mmio.h", "platform/platform.h", "gfx/gfx.h", "cores/spectrum_core.h"): (d / f).write_text((ROOT / "fw2" / f).read_text())
    h = (ROOT / "sim/p2_meter_harness.c").read_text().replace('"../fw2/ui/meter.c"', '"../ui/meter.c"').replace('"../fw2/platform/platform.h"', '"../platform/platform.h"')
    (d / "sim/h.c").write_text(h)
    exe = d / "h"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DP2_HOST", "-include", str(ROOT / "sim/p2_host_regs.h"), "-o", str(exe), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, seed): return int(subprocess.run([str(exe), str(seed), "3000"], capture_output=True, text=True).stdout.split()[1])
with tempfile.TemporaryDirectory() as td:
    good = build(td, False)
    check("the picture equals the clean render after every window (3 seeds)", all(run(good, s) == 0 for s in (1, 7, 99)))
with tempfile.TemporaryDirectory() as td:
    bad = build(td, True)
    check("mutant (the old erase) is caught", any(run(bad, s) > 0 for s in (1, 7, 99)))
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
