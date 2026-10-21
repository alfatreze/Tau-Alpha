#!/usr/bin/env python3
"""Page crossfade (fw2/ui/xfade.c, real graphics layer, engine model): the new page is held in the idle buffer, the shown page moves toward it in five steps and ends exactly on it (the last step is a copy), nothing flips, other
full redraws are refused meanwhile, finish() completes at once, and without the blend the frame flips as usual. Mutants: the schedule never reaches an exact copy; the hold is never released; the held buffer is not protected."""
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
    for sub in ("sim", "fw2/gfx", "fw2/platform", "fw2/ui", "fw"): (d / sub).mkdir(parents=True, exist_ok=True)
    x = (ROOT / "fw2/ui/xfade.c").read_text(); g = (ROOT / "fw2/gfx/gfx.c").read_text()
    def mx(a, b):
        nonlocal x
        assert a in x, a; x = x.replace(a, b, 1)
    def mg(a, b):
        nonlocal g
        assert a in g, a; g = g.replace(a, b, 1)
    if mutant == "inexact": mx("(step + 1u >= XF_N) ? 256u : 256u / (XF_N - step)", "(step + 1u >= XF_N) ? 250u : 256u / (XF_N - step)")
    if mutant == "norelease": mx("if (++step >= XF_N) { gfx_xfade_end(); step = 0; }", "if (++step >= XF_N) { step = 0; }")
    if mutant == "unprotected": mg("if (!caps_dbuf || held || (REG_RD(R_DBUF_DISP) & 2u)) return 0;", "if (!caps_dbuf || (REG_RD(R_DBUF_DISP) & 2u)) return 0;")
    (d / "fw2/ui/xfade.c").write_text(x); (d / "fw2/gfx/gfx.c").write_text(g)
    (d / "fw2/cores").mkdir(parents=True, exist_ok=True); (d / "fw2/cores/gfx_touch_core.h").write_text((ROOT / "fw2/cores/gfx_touch_core.h").read_text())
    for f in ("ui/xfade.h", "gfx/gfx.h", "platform/mmio.h", "platform/platform.h"): (d / "fw2" / f).write_text((ROOT / "fw2" / f).read_text())
    (d / "fw2/generated").mkdir(parents=True, exist_ok=True); (d / "fw2/generated/font_data.h").write_text((ROOT / "fw2/generated/font_data.h").read_text())
    (d / "sim/p2_engine_model.h").write_text((ROOT / "sim/p2_engine_model.h").read_text())
    (d / "sim/h.c").write_text((ROOT / "sim/p2_xfade_harness.c").read_text())
    exe = d / "h"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DP2_HOST", "-include", str(ROOT / "sim/p2_host_regs.h"), "-o", str(exe), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, scen):
    p = subprocess.run([str(exe), scen], capture_output=True, text=True); return p.stdout.strip().split("\n"), p.returncode
with tempfile.TemporaryDirectory() as td:
    g = build(td, None)
    for s in ("fade", "mid", "none"):
        out, rc = run(g, s)
        for l in out:
            if l.startswith("ok"): print(l)
            elif l: check(l, False)
for mut, scen in (("inexact", "fade"), ("norelease", "fade"), ("unprotected", "fade")):
    with tempfile.TemporaryDirectory() as td:
        out, rc = run(build(td, mut), scen); check(f"mutant {mut} is caught", rc != 0)
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
