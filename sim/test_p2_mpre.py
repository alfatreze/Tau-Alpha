#!/usr/bin/env python3
"""Meter preset bookkeeping (fw2/ui/meters.c): select validates (out of range = the first, an unknown meter is ignored), next wraps at each meter's own count, the module is told every change, and the names come from the module.
Mutants: next skips one, no range check."""
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
    for sub in ("ui", "gfx", "platform", "cores", "sim"): (d / sub).mkdir()
    src = (ROOT / "fw2/ui/meters.c").read_text()
    if mutant == "skips": a = "(pre[cur] + 1u) % meter_table[cur].npre"; assert a in src; src = src.replace(a, "(pre[cur] + 2u) % meter_table[cur].npre")
    if mutant == "norange": a = "if (p >= meter_table[m].npre) p = 0u;"; assert a in src; src = src.replace(a, "")
    (d / "ui/meters.c").write_text(src)
    for f in ("ui/meter.h", "ui/theme.h", "gfx/gfx.h", "platform/mmio.h", "platform/platform.h", "cores/spectrum_core.h"): (d / f).write_text((ROOT / "fw2" / f).read_text())
    (d / "sim/h.c").write_text((ROOT / "sim/p2_mpre_harness.c").read_text().replace('"../fw2/ui/meters.c"', '"../ui/meters.c"'))
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DP2_HOST", "-include", str(ROOT / "sim/p2_host_regs.h"), "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return subprocess.run([str(d / "h")], capture_output=True, text=True).stdout.strip().split("\n")
GOOD = ["of3 6 of2 0 last3 6 last2 0", "chladni_after5 1 last2 1 name chladni", "vu_wrap 0"]
with tempfile.TemporaryDirectory() as td:
    check("select validates, next wraps per meter, names come from the module", build(td, None) == GOOD)
for m in ("skips", "norange"):
    with tempfile.TemporaryDirectory() as td: check(f"mutant {m} is caught", build(td, m) != GOOD)
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
