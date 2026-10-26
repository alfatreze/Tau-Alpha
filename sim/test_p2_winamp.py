#!/usr/bin/env python3
"""Winamp bars (fw2/ui/meter_winamp_bars.c): the drawn picture equals a clean render of the module's own state after every tick, over loud/quiet bursts, pauses and forced repaints (stale pixels must be wiped).
The mutant erases above the bar in the bar colour instead of the background (a shrinking bar under a held cap would stay tall)."""
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
    for sub in ("ui", "gfx", "generated", "sim"): (d / sub).mkdir()
    src = (ROOT / "fw2/ui/meter_winamp_bars.c").read_text().replace('#include "../cores/meter_core.h"', '#include "' + str(ROOT / "fw2/cores/meter_core.h") + '"')
    if mutant:
        a = "gfx_bar(x, y, colw, h, bh, UI_ACCENT, in->bg);"
        assert a in src
        src = src.replace(a, "gfx_bar(x, y, colw, h, bh, UI_ACCENT, UI_ACCENT);")
    (d / "ui/meter_winamp_bars.c").write_text(src)
    for f in ("ui/meter.h", "ui/theme.h", "gfx/gfx.h", "generated/meter_presets.h"): (d / f).write_text((ROOT / "fw2" / f).read_text())
    (d / "sim/h.c").write_text((ROOT / "sim/p2_winamp_harness.c").read_text().replace('"../fw2/ui/meter_winamp_bars.c"', '"../ui/meter_winamp_bars.c"'))
    exe = d / "h"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-o", str(exe), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, seed): return int(subprocess.run([str(exe), str(seed), "1500"], capture_output=True, text=True).stdout.split()[1])
with tempfile.TemporaryDirectory() as td:
    good = build(td, False)
    check("the picture equals the clean render after every tick (3 seeds)", all(run(good, s) == 0 for s in (1, 7, 99)))
with tempfile.TemporaryDirectory() as td:
    bad = build(td, True)
    check("mutant (the rows above the bar keep the bar colour) is caught", any(run(bad, s) > 0 for s in (1, 7, 99)))
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
