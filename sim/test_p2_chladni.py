#!/usr/bin/env python3
"""Chladni meter (fw2/ui/meter_chladni.c) against a model of the draw engine and the SDRAM mailbox: the box equals the tile plane scaled and repeated across it after every figure, for a mailbox with the normal and the
swapped half order; a dead mailbox makes the meter unavailable (it says so, draws no figure). Mutants: no downward repeat, ignoring the probe's swap result."""
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
    src = (ROOT / "fw2/ui/meter_chladni.c").read_text().replace('"../../fw/chladni_core.h"', f'"{ROOT}/fw/chladni_core.h"').replace('"../cores/ramp_core.h"', f'"{ROOT}/fw2/cores/ramp_core.h"')
    if mutant == "norepeat":
        a = "    have = rows;\n    while (have < h) {"
        assert a in src; src = src.replace(a, "    have = rows;\n    while (0) {")
    if mutant == "noswap":
        a = "uint32_t d = swap ? ((p0 << 16) | p1) : ((p1 << 16) | p0);"
        assert a in src; src = src.replace(a, "uint32_t d = (p1 << 16) | p0;")
    (d / "ui/meter_chladni.c").write_text(src)
    for f in ("ui/meter.h", "ui/theme.h", "gfx/gfx.h", "generated/meter_presets.h", "platform/mmio.h"): (d / f).write_text((ROOT / "fw2" / f).read_text())
    (d / "sim/h.c").write_text((ROOT / "sim/p2_chladni_harness.c").read_text().replace('"../fw2/ui/meter_chladni.c"', '"../ui/meter_chladni.c"'))
    exe = d / "h"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DP2_HOST", "-include", str(ROOT / "sim/p2_host_regs.h"), "-o", str(exe), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, seed, swap=0, broken=0):
    o = subprocess.run([str(exe), str(seed), "300", str(swap), str(broken)], capture_output=True, text=True).stdout.split()
    return int(o[1]), int(o[3]), int(o[5])
with tempfile.TemporaryDirectory() as td:
    g = build(td, None)
    for s in (1, 7, 99):
        m, f, u = run(g, s); check(f"seed {s}: {f} figures, box equals the scaled, repeated plane", m == 0 and f > 5 and u == 0)
        m, f, u = run(g, s, swap=1); check(f"seed {s}: swapped mailbox halves still give a correct picture", m == 0 and f > 5 and u == 0)
    m, f, u = run(g, 1, broken=1); check("a dead mailbox makes the meter unavailable and draws no figure", f == 0 and u == 1)
for mut in ("norepeat", "noswap"):
    with tempfile.TemporaryDirectory() as td:
        b = build(td, mut)
        caught = any(run(b, s, sw)[0] > 0 for s in (1, 7) for sw in (0, 1))
        check(f"mutant {mut} is caught", caught)
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
