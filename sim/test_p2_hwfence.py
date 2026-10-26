#!/usr/bin/env python3
"""Talos T3 firmware side: the hardware fence of fw2/gfx/gfx.c (OP_FENCE through R_FB_GO, the retired-fence counter at R_GFX_FENCE) against the engine model: the
probe picks it on a bitstream with the status block and the old mailbox fence on one without; a fence counter that never advances falls back after 50 ms instead
of hanging. Mutants: the firmware does not wait for the counter; the probe always says yes; the fence is counted as a draw command."""
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
    for sub in ("sim", "fw2/gfx", "fw2/platform", "fw2/cores", "fw2/generated"): (d / sub).mkdir(parents=True, exist_ok=True)
    src = (ROOT / "fw2/gfx/gfx.c").read_text()
    def m(a, b):
        nonlocal src
        assert a in src, a; src = src.replace(a, b, 1)
    if mutant == "nowait": m("if (hw) { while ((int32_t)(REG_RD(R_GFX_FENCE) - hw_fence_issued) < 0)", "if (hw) { while (0)")
    if mutant == "probe": m("hw_fence = (REG_RD(R_GFX_STATUS) >> 31) ? 1 : 0;", "hw_fence = 1;")
    if mutant == "counted": m("REG_WR(R_FB_GO, OP_FENCE);", "FB_GO(OP_FENCE);")
    if mutant == "nofallback": m("{ r = 0xDEADBEEFu; hw_fence = 0; break; }", "{ r = 0xDEADBEEFu; break; }")
    (d / "fw2/gfx/gfx.c").write_text(src)
    (d / "fw2/cores/gfx_touch_core.h").write_text((ROOT / "fw2/cores/gfx_touch_core.h").read_text())
    for f in ("gfx/gfx.h", "platform/mmio.h", "platform/platform.h"): (d / "fw2" / f).write_text((ROOT / "fw2" / f).read_text())
    (d / "fw2/generated/font_data.h").write_text((ROOT / "fw2/generated/font_data.h").read_text())
    (d / "sim/p2_engine_model.h").write_text((ROOT / "sim/p2_engine_model.h").read_text())
    (d / "sim/h.c").write_text((ROOT / "sim/p2_hwfence_harness.c").read_text())
    exe = d / "h"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DP2_HOST", "-include", str(ROOT / "sim/p2_host_regs.h"), "-o", str(exe), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, scen):
    p = subprocess.run([str(exe), scen], capture_output=True, text=True, timeout=120); return p.stdout.strip().split("\n"), p.returncode
with tempfile.TemporaryDirectory() as td:
    g = build(td, None)
    for s in ("hw", "old", "stuck"):
        out, rc = run(g, s)
        for l in out:
            if l.startswith("ok"): print(l)
            elif l: check(l, False)
        if rc: check(f"scenario {s} exit code", False)
for mut, scen in (("nowait", "hw"), ("probe", "old"), ("counted", "hw"), ("nofallback", "stuck")):
    with tempfile.TemporaryDirectory() as td:
        try: out, rc = run(build(td, mut), scen)
        except subprocess.TimeoutExpired: out, rc = [], 1
        check(f"mutant {mut} is caught", rc != 0)
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
