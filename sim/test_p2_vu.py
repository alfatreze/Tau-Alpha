#!/usr/bin/env python3
"""Master VU (fw2/ui/meter_vu_master.c): the ladders equal a clean render of the module's state after every tick (zone runs, held peak marker above the lit run), over loud/quiet bursts, pauses and forced
repaints with stale pixels. The mutant draws the lit zones without first clearing the ladder row (a falling level would leave old segments lit)."""
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
    src = (ROOT / "fw2/ui/meter_vu_master.c").read_text()
    for f, sd in (("meter_core", "cores"), ("vu_master_core", "cores"), ("vu_segment_table", "generated")): src = src.replace(f'#include "../{sd}/{f}.h"', f'#include "{ROOT}/fw2/{sd}/{f}.h"')
    if mutant:
        a = "    gfx_rect(lx, ly, lw, ROW_H, bg);\n    uint32_t zone[3]"
        assert a in src
        src = src.replace(a, "    uint32_t zone[3]")
    (d / "ui/meter_vu_master.c").write_text(src)
    for f in ("ui/meter.h", "ui/theme.h", "gfx/gfx.h", "generated/meter_presets.h"): (d / f).write_text((ROOT / "fw2" / f).read_text())
    (d / "sim/h.c").write_text((ROOT / "sim/p2_vu_harness.c").read_text().replace('"../fw2/ui/meter_vu_master.c"', '"../ui/meter_vu_master.c"'))
    exe = d / "h"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-o", str(exe), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, seed): return int(subprocess.run([str(exe), str(seed), "1500"], capture_output=True, text=True).stdout.split()[1])
with tempfile.TemporaryDirectory() as td:
    g = build(td, False)
    check("the ladders equal the clean render after every tick (3 seeds)", all(run(g, s) == 0 for s in (1, 7, 99)))
with tempfile.TemporaryDirectory() as td:
    bad = build(td, True)
    check("mutant (no clear of the row) is caught", any(run(bad, s) > 0 for s in (1, 7, 99)))
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
