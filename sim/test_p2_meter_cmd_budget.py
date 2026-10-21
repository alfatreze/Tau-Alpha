#!/usr/bin/env python3
"""The per-meter draw-command budget (fw2/cores/meter_budget_core.h, used by fw2/ui/meters.c; manifest p2.cmds): the commands a drawn tick issues (the gfx_cmds counter, which every engine command passes) are judged
against the meter's ceiling. A tick over it raises the meter's own degrade level (0..2: every 2nd / 4th window drawn, the meter is told through mtr_in_t.degrade); MB_RECOVER clean ticks lower it again; a forced repaint is not
judged; the governor's level wins when it is higher; every meter has its own level. Beam exposure: the rows a tick wrote (gfx_touch) against the beam (helios_core.h): inside = exposed, ahead / behind / blanking / no counter / nothing visible / stash only = not. Mutants: a tick exactly at the ceiling counts as over; no hysteresis; nothing is judged; forced repaints are judged; the own level is ignored."""
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
    src = (ROOT / "fw2/ui/meters.c").read_text(); flags = []
    if mutant == "nojudge": a = "mb_judge(&budget[cur], meter_stats.cmds_last, meter_cmd_budget[cur]);"; assert a in src; src = src.replace(a, "")
    if mutant == "judgeforce": a = "if (!in.force) { mb_judge("; assert a in src; src = src.replace(a, "if (1) { mb_judge(")
    if mutant == "ownignored": a = "mb_effective(mp3play_degrade_now(), budget[cur].level)"; assert a in src; src = src.replace(a, "mp3play_degrade_now()")
    if mutant == "noexpose": a = "if (!hl_rows_safe("; assert a in src; src = src.replace(a, "if (0 && !hl_rows_safe(")
    if mutant == "exposeall": a = "if (!hl_rows_safe("; assert a in src; src = src.replace(a, "if (1 || !hl_rows_safe(")
    if mutant == "atceiling": flags = ["-DBUG=1"]
    if mutant == "nohysteresis": flags = ["-DBUG=2"]
    (d / "ui/meters.c").write_text(src)
    for f in ("ui/meter.h", "ui/theme.h", "gfx/gfx.h", "platform/mmio.h", "platform/platform.h", "cores/spectrum_core.h", "cores/metr_core.h", "cores/tune_core.h", "cores/meter_budget_core.h", "cores/helios_core.h", "cores/gfx_touch_core.h", "generated/meter_presets.h", "generated/lw_presets.h", "generated/meter_registry.h"):
        (d / f).write_text((ROOT / "fw2" / f).read_text())
    (d / "sim/h.c").write_text((ROOT / "sim/p2_meter_budget_harness.c").read_text().replace('"../fw2/ui/meters.c"', '"../ui/meters.c"').replace('"../fw2/cores/gfx_touch_core.h"', '"../cores/gfx_touch_core.h"'))
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DP2_HOST"] + flags + ["-include", str(ROOT / "sim/p2_host_regs.h"), "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return d
def run(d): return subprocess.run([str(d / "h")], capture_output=True, text=True).stdout.strip().split("\n")
GOOD = ['ceiling bars 96', 'inside self 0 over 0 last 96', 'exactly at the ceiling: self 0 over 0', 'one over: self 1 over 1', 'max 97', 'over for 8 windows: drawn 3 self 2', 'the meter was told degrade 2', 'a forced repaint is not judged: over 4 self 2', 'back inside for 64 windows: drawn 16 self 1', 'and a while longer: self 0', "the governor's level wins when higher: told 1", 'another meter has its own level: vu self 1 bars self 2', 'beam inside: rows 50-150 direct 1 exposed 1', 'beam well ahead of the rows: direct 1 exposed 0', 'beam already past: direct 1 exposed 0', 'vertical blanking: direct 1 exposed 0', 'no beam counter: direct 1 exposed 0', 'nothing visible drawn: rows 0 direct 0', 'stash rows only: direct 0 exposed 0']
with tempfile.TemporaryDirectory() as td:
    out = run(build(td, None))
    for g, o in zip(GOOD, out): check(g, g == o)
    check("every step printed", len(out) == len(GOOD))
for m, name in (("atceiling", "a tick exactly at the ceiling counts as over"), ("nohysteresis", "one good tick lowers the level"), ("nojudge", "nothing is judged"), ("judgeforce", "forced repaints are judged"), ("ownignored", "the own level is ignored"), ("noexpose", "the beam exposure is never counted"), ("exposeall", "every direct tick counts as exposed")):
    with tempfile.TemporaryDirectory() as td:
        check("mutant (%s) is caught" % name, run(build(td, m)) != GOOD)
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
