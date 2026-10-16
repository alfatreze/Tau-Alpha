#!/usr/bin/env python3
"""Winamp Scope trail (fw2/ui/meter_winamp_scope.c, real graphics layer, engine model): an old trace that is not redrawn fades toward the background by exactly the blend of weight (100 - trail) percent each frame, and the
box's surroundings are untouched; trail 0 and a bitstream without the blend wipe the box. Mutants: the weight is the trail instead of its complement; the trail is ignored (wipe)."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(extra)))
    if not ok: fails += 1
def build(td, mutant):
    d = Path(td)
    for sub in ("sim", "fw2/gfx", "fw2/platform", "fw2/ui", "fw2/generated", "fw"): (d / sub).mkdir(parents=True, exist_ok=True)
    src = (ROOT / "fw2/ui/meter_winamp_scope.c").read_text()
    def m(a, b):
        nonlocal src
        assert a in src, a; src = src.replace(a, b, 1)
    if mutant == "inverse": m("uint32_t a = (100u - trail) * 256u / 100u;", "uint32_t a = trail * 256u / 100u;")
    if mutant == "ignored": m("if (trail && gfx_blend_ready()) {", "if (0) {")
    (d / "fw2/ui/meter_winamp_scope.c").write_text(src)
    for f in ("gfx/gfx.h", "gfx/gfx.c", "platform/mmio.h", "platform/platform.h", "ui/meter.h", "ui/theme.h", "generated/meter_presets.h"): (d / "fw2" / f).write_text((ROOT / "fw2" / f).read_text())
    (d / "fw/font_metrics.h").write_text((ROOT / "fw/font_metrics.h").read_text())
    (d / "sim/p2_engine_model.h").write_text((ROOT / "sim/p2_engine_model.h").read_text())
    (d / "sim/h.c").write_text((ROOT / "sim/p2_scope_trail_harness.c").read_text())
    exe = d / "h"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DP2_HOST", "-include", str(ROOT / "sim/p2_host_regs.h"), "-o", str(exe), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, trail, blend, frames=4):
    return subprocess.run([str(exe), str(trail), str(blend), str(frames)], capture_output=True, text=True).stdout.strip().split("\n")
def faded_ok(out):
    rows = [l.split() for l in out if l and l[0].isdigit() and l[0] != "0"]
    return len(rows) > 0 and all(r[1] == r[2] for r in rows)
with tempfile.TemporaryDirectory() as td:
    g = build(td, None)
    o = run(g, 30, 1); check("the first frame draws the trace in the accent colour", o[0].split()[1] == "fd05", o)
    check("with trail 30 the old trace fades by the blend, frame after frame (4 frames)", faded_ok(o), o)
    o = run(g, 60, 1, 6); check("with trail 60 the same, six frames", faded_ok(o), o)
    o = run(g, 30, 1); check("nothing outside the box changes", o[-1] == "OUTSIDE 0", o)
    o = run(g, 0, 1); check("trail 0 wipes the box (the old trace is gone at once)", all(l.split()[1] == "0000" for l in o if l and l[0] in "1234"), o)
    o = run(g, 30, 0); check("a bitstream without the blend wipes the box", all(l.split()[1] == "0000" for l in o if l and l[0] in "1234"), o)
    o = run(g, 30, 1, 1); check("the first faded value is strictly between the accent and the background", o[1].split()[1] not in ("fd05", "0000"), o)
for mut in ("inverse", "ignored"):
    with tempfile.TemporaryDirectory() as td:
        o = run(build(td, mut), 30, 1); check(f"mutant {mut} is caught", not faded_ok(o))
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
