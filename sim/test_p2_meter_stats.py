#!/usr/bin/env python3
"""Draw-cost counters of the meters (meter_stats in fw2/ui/meters.c): the cycles of each tick (compose copy included) and of the compose copy alone, the slowest tick of each meter, the number of ticks, and the
spectrum windows that waited because the governor shed the UI (counted once per window, the window is still drawn later). Degrade: at level 1 and 2 only every 2nd / 4th window is drawn, a forced repaint never skipped. Mutants: the maximum follows the last value; every shed call counts; the degrade level is ignored; a forced repaint can be skipped."""
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
    if mutant == "nodegrade": a = "if (dg && dg < 3u && !force_next) {"; assert a in src; src = src.replace(a, "if (0) {")
    if mutant == "skipforce": a = "if (dg && dg < 3u && !force_next) {"; assert a in src; src = src.replace(a, "if (dg && dg < 3u) {")
    if mutant == "ungated": a = "static int composed(const meter_def_t *d) { const uint32_t c = d->cmp & 0xFFFFu; return c == 1u || (c > 1u && cmp_gate(d->mv, c)); }"; assert a in src; src = src.replace(a, "static int composed(const meter_def_t *d) { return (d->cmp & 0xFFFFu) != 0u; }")
    if mutant == "everyshed": a = "if (w0 != win_seen && w0 != shed_win) { meter_stats.shed++; shed_win = w0; }"; assert a in src; src = src.replace(a, "if (w0 != win_seen) { meter_stats.shed++; shed_win = w0; }")
    (d / "ui/meters.c").write_text(src)
    for f in ("ui/meter.h", "ui/theme.h", "gfx/gfx.h", "platform/mmio.h", "platform/platform.h", "cores/spectrum_core.h", "cores/metr_core.h", "cores/tune_core.h", "cores/meter_budget_core.h", "cores/pack_strike_core.h", "cores/meter_desc_core.h", "cores/meter_pack_core.h", "cores/library_core.h", "ui/meter_pack.h", "cores/helios_core.h", "cores/gfx_touch_core.h", "generated/meter_presets.h", "generated/lw_presets.h", "generated/meter_registry.h"):
        (d / f).write_text((ROOT / "fw2" / f).read_text())
    (d / "sim/h.c").write_text((ROOT / "sim/p2_meter_stats_harness.c").read_text().replace('"../fw2/ui/meters.c"', '"../ui/meters.c"').replace('"../fw2/generated/meter_registry.h"', '"../generated/meter_registry.h"'))
    (d / "sim/p2_dyn_register.h").write_text((ROOT / "sim/p2_dyn_register.h").read_text())
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DP2_HOST", "-include", str(ROOT / "sim/p2_host_regs.h"), "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return d
sys.path.insert(0, str(ROOT / "tools/player2"))
import meter_desc as _md
DESCS = ["winamp_scope", "chladni", "layered_wave", "vu_master", "waterfall_grid"]         # Winamp Bars is built in; the other meters are rows through their descriptors
def run(d):
    for k in DESCS: (d / ("%s.desc" % k)).write_bytes(_md.for_meter(k))
    return subprocess.run([str(d / "h")] + [str(d / ("%s.desc" % k)) for k in DESCS], capture_output=True, text=True).stdout.strip().split("\n")
GOOD = ["rows 5 count 6", "a ticks 1 shed 0 last 100 compose 0 max 100 0 0 0 0",
        "b ticks 2 shed 0 last 340 compose 40 max 100 0 340 0 0",
        "c ticks 2 shed 1 last 340 compose 40 max 100 0 340 0 0",
        "d ticks 3 shed 1 last 340 compose 40 max 100 0 340 0 0",
        "e ticks 3 shed 1 last 340 compose 40 max 100 0 340 0 0",
        "f ticks 4 shed 1 last 940 compose 40 max 100 0 940 0 0",
        "g ticks 5 shed 1 last 90 compose 40 max 100 0 940 0 0",
        "deg1 drawn 4 skipped 4", "deg2 drawn 2 skipped 6", "force drawn 1 skipped 0", "deg0 drawn 3", "spectrogram composed 4", "zones composed 0"]
import re
METER_N = int(re.search(r"#define METER_SLOTS (\d+)u", (ROOT / "fw2/generated/meter_registry.h").read_text()).group(1))
GOOD = [(g + " 0" * (METER_N - 5)) if g[0] in "abcdefg" and len(g.split()[0]) == 1 else g for g in GOOD]       # the expectations are written for five meters: the others have not drawn
with tempfile.TemporaryDirectory() as td:
    out = run(build(td, None))
    for g, o in zip(GOOD, out): check(g, g == o)
    check("every step printed", len(out) == len(GOOD))
for m, name in (("lastmax", "the maximum follows the last value"), ("everyshed", "every shed call counts"), ("nodegrade", "the degrade level is ignored"), ("skipforce", "a forced repaint can be skipped"), ("ungated", "the compose gate is ignored")):
    with tempfile.TemporaryDirectory() as td:
        check("mutant (%s) is caught" % name, run(build(td, m)) != GOOD)
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
