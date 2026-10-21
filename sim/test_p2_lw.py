#!/usr/bin/env python3
"""Layered Wave wrapper (fw2/ui/meter_layered_wave.c): all 16 presets (8 standard, 8 experimental) draw inside the box and fill the history ring when the PSRAM window is proven; without the window the meter says so, draws no figure and stores nothing.
Mutants: the window proof is skipped (stores into an unproven window), the preset is ignored (every preset draws the first)."""
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
    for sub in ("ui", "gfx", "platform", "generated", "sim"): (d / sub).mkdir()
    src = (ROOT / "fw2/ui/meter_layered_wave.c").read_text()
    src = src.replace('#include "layered_wave.inc"', f'#include "{ROOT}/fw2/ui/layered_wave.inc"')
    if mutant == "noproof":
        a = "    if (!lw_ps) {"; assert a in src; src = src.replace(a, "    if (!lw_ps) { lw_ps = 1u; } if (0) {")
    if mutant == "nopreset":
        a = "lw_mv[k] = lw_preset[want][k]"; assert a in src; src = src.replace(a, "lw_mv[k] = lw_preset[0][k]")
    (d / "ui/meter_layered_wave.c").write_text(src)
    for f in ("ui/meter.h", "ui/theme.h", "gfx/gfx.h", "platform/mmio.h", "generated/lw_presets.h"): (d / f).write_text((ROOT / "fw2" / f).read_text())
    (d / "sim/h.c").write_text((ROOT / "sim/p2_lw_harness.c").read_text().replace('"../fw2/ui/meter_layered_wave.c"', '"../ui/meter_layered_wave.c"'))
    exe = d / "h"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DP2_HOST", "-include", str(ROOT / "sim/p2_host_regs.h"), "-I", str(ROOT / "fw"), "-o", str(exe), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, seed, psram=1):
    o = subprocess.run([str(exe), str(seed), "120", str(psram)], capture_output=True, text=True).stdout.split()
    return dict(zip(o[0::2], map(int, o[1::2])))
with tempfile.TemporaryDirectory() as td:
    g = build(td, None)
    for s in (1, 7):
        r = run(g, s); check(f"seed {s}: all 16 presets draw, inside the box, the ring fills", r["OUTSIDE"] == 0 and r["PRESETS"] == 16 and r["RING"] > 0 and r["TEXT"] == 0)
    r = run(g, 1, 0); check("no PSRAM window: a message, no store into the ring", r["RING"] == 0 and r["TEXT"] > 0)
with tempfile.TemporaryDirectory() as td:
    b = build(td, "noproof"); r = run(b, 1, 0)
    check("mutant noproof is caught", r["RING"] > 0 or r["TEXT"] == 0)
with tempfile.TemporaryDirectory() as td:
    good = run(build(td, None), 1)["RECTS"]
with tempfile.TemporaryDirectory() as td:
    b = run(build(td, "nopreset"), 1)["RECTS"]
    check("mutant nopreset is caught (the presets draw differently)", b != good)
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
