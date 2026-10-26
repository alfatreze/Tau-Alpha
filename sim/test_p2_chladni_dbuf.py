#!/usr/bin/env python3
"""Chladni composed in the idle buffer (real graphics layer + engine model): the shown buffer's box equals the scaled, repeated tile plane after every figure, with the displayed buffer 0 and after a flip (1): the blit-mode commands
follow the drawing target (the old player's B-414 bug was a Chladni that drew into buffer 0 whatever was shown). Mutant: the graphics layer's blit bases ignore the target."""
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
    g = (ROOT / "fw2/gfx/gfx.c").read_text()
    if mutant == "bases":
        a = "void gfx_blit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) { blit_op(sx, sy, dx, dy, w, h, OP_BLIT, buf_base(cpu_buf), buf_base(cpu_buf)); }"; assert a in g
        g = g.replace(a, "void gfx_blit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) { blit_op(sx, sy, dx, dy, w, h, OP_BLIT, 0u, 0u); }")
    (d / "fw2/gfx/gfx.c").write_text(g)
    c = (ROOT / "fw2/ui/meter_chladni.c").read_text().replace('"../cores/chladni_core.h"', f'"{ROOT}/fw2/cores/chladni_core.h"').replace('"../cores/ramp_core.h"', f'"{ROOT}/fw2/cores/ramp_core.h"')
    (d / "fw2/ui/meter_chladni.c").write_text(c)
    (d / "fw2/cores").mkdir(parents=True, exist_ok=True); (d / "fw2/cores/gfx_touch_core.h").write_text((ROOT / "fw2/cores/gfx_touch_core.h").read_text())
    for f in ("gfx/gfx.h", "platform/mmio.h", "platform/platform.h", "ui/meter.h", "ui/theme.h", "generated/meter_presets.h"): (d / "fw2" / f).write_text((ROOT / "fw2" / f).read_text())
    (d / "fw2/generated").mkdir(parents=True, exist_ok=True); (d / "fw2/generated/font_data.h").write_text((ROOT / "fw2/generated/font_data.h").read_text())
    (d / "sim/p2_engine_model.h").write_text((ROOT / "sim/p2_engine_model.h").read_text())
    (d / "sim/h.c").write_text((ROOT / "sim/p2_chladni_dbuf_harness.c").read_text())
    exe = d / "h"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DP2_HOST", "-include", str(ROOT / "sim/p2_host_regs.h"), "-o", str(exe), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, seed):
    o = subprocess.run([str(exe), str(seed), "120"], capture_output=True, text=True).stdout.split()
    return dict(zip(o[0::2], map(int, o[1::2])))
with tempfile.TemporaryDirectory() as td:
    g = build(td, None)
    for s in (1, 7):
        r = run(g, s); check(f"seed {s}: {r.get('FIGURES')} figures, shown box equals the scaled, repeated plane in both buffers, target restored, nothing outside", r.get("MISMATCH") == 0 and r.get("FIGURES", 0) > 10 and r.get("OUTSIDE") == 0 and r.get("TARGETBAD") == 0, r)
with tempfile.TemporaryDirectory() as td:
    r = run(build(td, "bases"), 1); check("mutant bases (a copy ignores the target) is caught", r.get("MISMATCH", 0) > 0, r)
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
