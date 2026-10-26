#!/usr/bin/env python3
"""The BLIT OPS check (fw2/diagnostics/blit_check.c) against the host model of the draw engine (sim/p2_engine_model.h): the expectations must hold on the model (so a FAIL on a Pocket means the engine
differs from the model), and each mutant of the model or of the graphics layer must fail the test it is meant to (so the check can fail)."""
import re, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    if not ok: fails += 1
def build(td, model_mut=None, gfx_mut=None):
    d = Path(td)
    for sub in ("sim", "fw2/gfx", "fw2/platform", "fw2/cores", "fw2/generated", "fw2/diagnostics", "fw2/ui", "fw"): (d / sub).mkdir(parents=True, exist_ok=True)
    gfx = (ROOT / "fw2/gfx/gfx.c").read_text(); model = (ROOT / "sim/p2_engine_model.h").read_text()
    if gfx_mut: a, b = gfx_mut; assert a in gfx, a; gfx = gfx.replace(a, b, 1)
    if model_mut: a, b = model_mut; assert a in model, a; model = model.replace(a, b, 1)
    (d / "fw2/gfx/gfx.c").write_text(gfx); (d / "sim/p2_engine_model.h").write_text(model)
    (d / "fw2/cores/gfx_touch_core.h").write_text((ROOT / "fw2/cores/gfx_touch_core.h").read_text())
    for f in ("gfx/gfx.h", "platform/mmio.h", "platform/platform.h", "diagnostics/blit_check.c", "diagnostics/blit_check.h"): (d / "fw2" / f).write_text((ROOT / "fw2" / f).read_text())
    (d / "fw2/generated/font_data.h").write_text((ROOT / "fw2/generated/font_data.h").read_text())
    (d / "sim/h.c").write_text((ROOT / "sim/p2_blitcheck_harness.c").read_text())
    exe = d / "h"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DP2_HOST", "-include", str(ROOT / "sim/p2_host_regs.h"), "-o", str(exe), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe):
    o = subprocess.run([str(exe)], capture_output=True, text=True).stdout
    m = re.search(r"mask (\d+) bad (\d+)", o); return int(m.group(1)), int(m.group(2))
with tempfile.TemporaryDirectory() as td:
    mask, bad = run(build(td))
    check(f"every expectation holds on the engine model (mask {mask}, bad pixels {bad})", mask == 0 and bad == 0)
MUT = (
    ("RECT fill colour", ("(uint16_t)c->color;\n    } else if (op == 5u)", "(uint16_t)(c->color ^ 1u);\n    } else if (op == 5u)"), None, 0),
    ("BAR lit rule", ("j < h - lit ?", "j < lit ?"), None, 1),
    ("COPY source row step", ("(j / sy) * EM_STRIDE", "0u * EM_STRIDE"), None, 2),
    ("PALETTE index mask", ("em_clut[v & 255u]", "em_clut[v & 127u]"), None, 3),
    ("SCALED source column", ("(i / sx)", "(i)"), None, 4),
    ("BLEND shift", (">> 8) << 11)", ">> 7) << 11)"), None, 5),
    ("CLUT reload overtakes a queued blit", None, ("void gfx_clut_begin(uint32_t start) { wait_fifo(); fence_why(2u);", "void gfx_clut_begin(uint32_t start) { wait_fifo();"), 6),
)
for name, mm, gm, bit in MUT:
    with tempfile.TemporaryDirectory() as td:
        mask, bad = run(build(td, mm, gm))
    check(f"mutant '{name}' fails test bit {bit} (mask {mask})", bool(mask & (1 << bit)))
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
