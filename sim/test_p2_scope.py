#!/usr/bin/env python3
"""Winamp scope (fw2/ui/meter_winamp_scope.c) against a model of the capture block: the picture equals a clean render of the module's state, nothing is drawn outside the box (DC-biased and one-sided
captures included), and frames are drawn only when a capture has finished. The mutant clamps only one side of lo/hi (the old player's corruption)."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    if not ok: fails += 1
def build(td, mutant):
    d = Path(td)
    for sub in ("ui", "gfx", "platform", "sim"): (d / sub).mkdir()
    src = (ROOT / "fw2/ui/meter_winamp_scope.c").read_text()
    if mutant:
        a = "        if (lo < -ey) lo = -ey; else if (lo > ey) lo = ey;"
        b = "        if (hi < -ey) hi = -ey; else if (hi > ey) hi = ey;"
        assert a in src and b in src
        src = src.replace(a, "").replace(b, "")
        src = src.replace("        if (lo > hi) lo = hi;", "")
    (d / "ui/meter_winamp_scope.c").write_text(src)
    for f in ("ui/meter.h", "ui/theme.h", "gfx/gfx.h", "platform/mmio.h"): (d / f).write_text((ROOT / "fw2" / f).read_text())
    (d / "sim/h.c").write_text((ROOT / "sim/p2_scope_harness.c").read_text().replace('"../fw2/ui/meter_winamp_scope.c"', '"../ui/meter_winamp_scope.c"'))
    exe = d / "h"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DP2_HOST", "-include", str(ROOT / "sim/p2_host_regs.h"), "-o", str(exe), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, seed):
    o = subprocess.run([str(exe), str(seed), "400"], capture_output=True, text=True).stdout.split()
    return int(o[1]), int(o[3]), int(o[5])
with tempfile.TemporaryDirectory() as td:
    g = build(td, False)
    rs = [run(g, s) for s in (1, 7, 99)]
    check("the picture equals the clean render of the module's state", all(r[0] == 0 for r in rs))
    check("nothing is drawn outside the box, whatever the capture", all(r[1] == 0 for r in rs))
    check("frames are drawn (the model really exercised the draw path)", all(r[2] > 50 for r in rs))
with tempfile.TemporaryDirectory() as td:
    bad = build(td, True)
    check("mutant (no clamp of the envelope bounds) is caught", any(run(bad, s)[0] > 0 or run(bad, s)[1] > 0 for s in (1, 7, 99)))
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
