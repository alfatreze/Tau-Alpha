#!/usr/bin/env python3
"""Draw-cost counters of the meters (meter_stats in fw2/ui/meters.c): the cycles of each tick (compose copy included) and of the compose copy alone, the slowest tick of each meter, the number of ticks, and the
spectrum windows that waited because the governor shed the UI (counted once per window, the window is still drawn later). Mutants: the maximum follows the last value; every shed call counts."""
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
    for sub in ("ui", "gfx", "platform", "cores", "generated", "sim"): (d / sub).mkdir()
    src = (ROOT / "fw2/ui/meters.c").read_text()
    if mutant == "lastmax": a = "if (spent > meter_stats.max_cyc[cur]) meter_stats.max_cyc[cur] = spent;"; assert a in src; src = src.replace(a, "meter_stats.max_cyc[cur] = spent;")
    if mutant == "everyshed": a = "if (w0 != win_seen && w0 != shed_win) { meter_stats.shed++; shed_win = w0; }"; assert a in src; src = src.replace(a, "if (w0 != win_seen) { meter_stats.shed++; shed_win = w0; }")
    (d / "ui/meters.c").write_text(src)
    for f in ("ui/meter.h", "ui/theme.h", "gfx/gfx.h", "platform/mmio.h", "platform/platform.h", "cores/spectrum_core.h", "cores/metr_core.h", "cores/tune_core.h", "generated/meter_presets.h", "generated/lw_presets.h", "generated/meter_registry.h"):
        (d / f).write_text((ROOT / "fw2" / f).read_text())
    (d / "sim/h.c").write_text((ROOT / "sim/p2_meter_stats_harness.c").read_text().replace('"../fw2/ui/meters.c"', '"../ui/meters.c"'))
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DP2_HOST", "-include", str(ROOT / "sim/p2_host_regs.h"), "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return d
def run(d): return subprocess.run([str(d / "h")], capture_output=True, text=True).stdout.strip().split("\n")
GOOD = ["a ticks 1 shed 0 last 100 compose 0 max 100 0 0 0 0",
        "b ticks 2 shed 0 last 340 compose 40 max 100 0 340 0 0",
        "c ticks 2 shed 1 last 340 compose 40 max 100 0 340 0 0",
        "d ticks 3 shed 1 last 340 compose 40 max 100 0 340 0 0",
        "e ticks 3 shed 1 last 340 compose 40 max 100 0 340 0 0",
        "f ticks 4 shed 1 last 940 compose 40 max 100 0 940 0 0",
        "g ticks 5 shed 1 last 90 compose 40 max 100 0 940 0 0"]
with tempfile.TemporaryDirectory() as td:
    out = run(build(td, None))
    for g, o in zip(GOOD, out): check("%s: %s" % (g[0], g[2:]), g == o) if g != o else check("%s: %s" % (g[0], g[2:]), True)
    check("every step printed", len(out) == len(GOOD))
for m, name in (("lastmax", "the maximum follows the last value"), ("everyshed", "every shed call counts")):
    with tempfile.TemporaryDirectory() as td:
        check("mutant (%s) is caught" % name, run(build(td, m)) != GOOD)
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
