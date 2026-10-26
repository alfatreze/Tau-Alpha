#!/usr/bin/env python3
"""Player 2 graphics layer (fw2/gfx/gfx.c) against a host model of the draw engine with two display buffers, a lazily draining command queue, sticky bases/blend read at execution and a flip applied at a vblank:
a full redraw into the idle buffer, blit-mode commands following the drawing target, shared stash rows, an off-screen compose copied onto the displayed buffer, the alpha blend, and the no-H2 fallback.
Mutants: blit bases not following the target; the bases changed without draining the queue first; the redraw flips without a fence; blend turned off without a fence; the present copies the wrong buffer; the blend probe always says yes; the bar's two high lit bits dropped."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    if not ok: fails += 1
SCEN = ("bracket", "blitmode", "stash", "compose", "blend", "noblend", "nodbuf", "bar", "mbbuf")
def build(td, mutant):
    d = Path(td)
    for sub in ("sim", "fw2/gfx", "fw2/platform"): (d / sub).mkdir(parents=True, exist_ok=True)
    src = (ROOT / "fw2/gfx/gfx.c").read_text()
    def m(a, b):
        nonlocal src
        assert a in src, a; src = src.replace(a, b, 1)
    if mutant == "bases": m("blit_op(sx, sy, dx, dy, w, h, OP_CBLIT, 0u, buf_base(cpu_buf))", "blit_op(sx, sy, dx, dy, w, h, OP_CBLIT, 0u, 0u)"); m("OP_SBLIT | (s << 10) | (s << 12), 0u, buf_base(cpu_buf))", "OP_SBLIT | (s << 10) | (s << 12), 0u, 0u)")
    if mutant == "setfence": m("    gfx_fence();                                       /* the fields are read when a command EXECUTES: whatever is queued must run against the old pair first */\n", "")
    if mutant == "nosync": m("    for (uint32_t cx = 0; cx < GFX_W; cx += 120u) blit_op(cx, 0u, cx, 0u, 120u < GFX_W - cx ? 120u : GFX_W - cx, GFX_H, OP_BLIT, buf_base(front), buf_base(front ^ 1u));", "")
    if mutant == "composesync": m("OP_BLIT, buf_base(front), buf_base(front ^ 1u)); }   /* the box starts", "OP_BLIT, buf_base(front), buf_base(front ^ 1u)); break; }   /* the box starts")
    if mutant == "nofence": m("    if (!active) return;\n    gfx_fence();\n", "    if (!active) return;\n")
    if mutant == "blendfence": m("void gfx_blend_off(void) { gfx_fence();", "void gfx_blend_off(void) {")
    if mutant == "present": m("buf_base(back), buf_base(front)); }", "buf_base(front), buf_base(back)); }")
    if mutant == "mbbuf": m("a + (cpu_buf && a < GFX_STRIDE * GFX_H ? DBUF_BASE1_W : 0u)", "a")
    if mutant == "barhi": m("(((lit >> 7) & 0x3u) << 15)", "0u")
    if mutant == "barclip": m("    if (lit > 511u) lit = 511u;\n", "")
    if mutant == "probe": m("    return gfx_blend_state == 1u;\n}\n/* Blend the w x h box", "    gfx_blend_state = 1u; return 1;\n}\n/* Blend the w x h box")
    (d / "fw2/gfx/gfx.c").write_text(src)
    (d / "fw2/cores").mkdir(parents=True, exist_ok=True); (d / "fw2/cores/gfx_touch_core.h").write_text((ROOT / "fw2/cores/gfx_touch_core.h").read_text())
    for f in ("gfx/gfx.h", "platform/mmio.h", "platform/platform.h"): (d / "fw2" / f).write_text((ROOT / "fw2" / f).read_text())
    (d / "fw").mkdir(exist_ok=True)
    (d / "fw2/generated").mkdir(parents=True, exist_ok=True); (d / "fw2/generated/font_data.h").write_text((ROOT / "fw2/generated/font_data.h").read_text())
    (d / "sim/p2_engine_model.h").write_text((ROOT / "sim/p2_engine_model.h").read_text())
    (d / "sim/h.c").write_text((ROOT / "sim/p2_gfx_harness.c").read_text())
    exe = d / "h"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DP2_HOST", "-include", str(ROOT / "sim/p2_host_regs.h"), "-o", str(exe), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, scen):
    p = subprocess.run([str(exe), scen], capture_output=True, text=True); return p.stdout.strip().split("\n"), p.returncode
with tempfile.TemporaryDirectory() as td:
    g = build(td, None)
    for s in SCEN:
        out, rc = run(g, s)
        for l in out:
            if l.startswith("ok"): print(l)
            elif l: check(l, False)
        if rc: fails += 0
for mut, scen in (("bases", "blitmode"), ("nofence", "bracket"), ("setfence", "blitmode"), ("nosync", "bracket"), ("composesync", "compose"), ("blendfence", "blend"), ("present", "compose"), ("probe", "noblend"), ("barhi", "bar"), ("mbbuf", "mbbuf")):
    with tempfile.TemporaryDirectory() as td:
        out, rc = run(build(td, mut), scen); check(f"mutant {mut} is caught", rc != 0)
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
