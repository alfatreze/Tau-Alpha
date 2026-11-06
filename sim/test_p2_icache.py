#!/usr/bin/env python3
"""Instruction-cache conflict budget for the Layered Wave pack (docs/features/LAYERED_WAVE_PERF_PLAN.md, P1).

The CPU runs pack code from PSRAM through a 4 KiB direct-mapped instruction cache (128 lines of 32 bytes). Two hot pieces of code whose addresses are 4 KiB apart evict each other on every pass, and each miss
costs about 253 cycles: before the plan's fix, Layered Wave's history view spent most of its time that way (B-944). This test runs the pack as shipped (linked at the simulator slot, 4 KiB aligned like the real one, so
the cache indices equal the Pocket's) through a copy of tools/rv32sim.py that models the cache for addresses at or above the pack, for every preset, and fails if one tick costs more than a budget of modelled misses.
  1. per preset, in the 368 x 122 window box: marginal misses per tick = (misses over 13 frames - over 3 frames) / 10, at most BUDGET;
  2. the cost in modelled cycles (instructions x 1.65 + misses x 253) stays under the per-tick cycle budget;
  3. controls: the pack built without the three `noinline` attributes on the view functions (the layout of the old player), and the pack with `noinline` but without the section placement of the hot helpers and views
     (fw2/ui/meter_pack.ld, `.text.pk_hot` / `.text.pk_view1..3`: the DEV 205 test pack), must each FAIL (1), so the test is known to see the problem.
The model constants (253 cycles per fill, 1.65 per instruction) are from docs/PHASE_G_SPEC.md and docs/HOW_IT_WORKS.md; they are not validated on a Pocket for this code. Host-simulated evidence only."""
import re, shutil, subprocess, sys, tempfile, os
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/player2")); sys.path.insert(0, str(ROOT / "tools"))
import pack_meter as pm
SIM_ORG, SIM_SCRATCH = 0x00400000, 0x00300000
SIMREGS = ["-include", str(ROOT / "sim/p2_pack_sim_regs.h")]
N_PRESETS = 16
BUDGET_MISSES = 500             # per tick, worst preset. Measured with the placement in place: 307 (the per-tick cold code and the rest of the once-per-tick work); without it 3013, without noinline 35485
BUDGET_CYCLES = 1_300_000       # modelled cycles per tick, worst preset (measured 1.07 M at the busiest preset)
fails = 0
def check(name, cond, info=""):
    global fails
    print(("ok   " if cond else "FAIL ") + name + ("  " + info if info and not cond else ""))
    fails += 0 if cond else 1

_src = (ROOT / "tools/rv32sim.py").read_text()
_old = "        while n < max_instr:\n            n += 1\n"
assert _old in _src, "tools/rv32sim.py main loop changed: update the cache hook"
_src = _src.replace(_old, "        T = self.T\n        while n < max_instr:\n            n += 1\n            if pc >= 0x400000:\n                ln = pc >> 5; ix = ln & 127\n                if T[ix] != ln:\n                    T[ix] = ln; self.miss += 1\n", 1)
_ns = {"__name__": "rv32cache"}; exec(compile(_src, "rv32cache", "exec"), _ns)
import importlib.util
_sp = importlib.util.spec_from_file_location("devmodel", ROOT / "tools/p2_dev_model.py")

def sim(elf, blob):
    """(instructions, modelled misses) of one run"""
    m = _ns["Machine"](); m.T = [-1] * 128; m.miss = 0
    m.dev = importlib.util.module_from_spec(_sp); _sp.loader.exec_module(m.dev)
    m.load_elf(str(elf)); m.blob = blob
    m.run()
    return m.icount, m.miss

def harness(tmp, preset, frames):
    elf = tmp / ("h_%d_%d.elf" % (preset, frames))
    cmd = [pm.tool("gcc"), "-march=rv32im", "-mabi=ilp32", "-mno-relax", "-O2", "-ffreestanding", "-nostdlib", "-nostartfiles", "-w"] + SIMREGS + [
           "-DPACK_LW", "-DTR_NOHASH", "-DLW_P=%d" % preset, "-DLW_FS=0", "-DNF=%d" % frames, "-DPACK_ORG=0x%X" % SIM_ORG, "-DSCRATCH_ORG=0x%X" % SIM_SCRATCH,
           "-DPACK_METER=%d" % pm.METER_IDS["layered_wave"], "-DPACK_SLOT=%d" % pm.SLOT["layered_wave"], "-Wl,--no-warn-rwx-segments", "-T", str(ROOT / "tools/host/link.ld"),
           str(ROOT / "tools/host/start.S"), str(ROOT / "tools/host/p2_lw_cost_harness.c"), "-I", str(ROOT / "fw2/ui"), "-I", str(ROOT / "fw2/cores"), "-I", str(ROOT / "fw"), "-I", str(ROOT / "tools/host"), "-o", str(elf), "-lgcc"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return elf

def per_tick(tmp, blob, preset):
    """marginal (instructions, misses) per tick: 13 frames minus 3 frames, over 10"""
    a = sim(harness(tmp, preset, 13), blob); b = sim(harness(tmp, preset, 3), blob)
    return (a[0] - b[0]) / 10.0, (a[1] - b[1]) / 10.0

def sweep(tmp, blob, presets):
    import contextlib, io
    with contextlib.redirect_stdout(io.StringIO()), ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        return list(ex.map(lambda p: per_tick(tmp, blob, p), presets))

def mutant_blob(tmp, name, edits):
    """the pack built from a copy of the tree with `edits` applied: a list of (file under fw2/ui, old text, new text)"""
    T = tmp / name; (T / "fw2/ui").mkdir(parents=True)
    for e in (ROOT / "fw2").iterdir():
        if e.name != "ui": os.symlink(e, T / "fw2" / e.name)
    for e in (ROOT / "fw2/ui").iterdir(): os.symlink(e, T / "fw2/ui" / e.name)
    for n in ("fw", "meters", "toolchain"): os.symlink(ROOT / n, T / n)
    for f, old, new in edits:
        txt = (ROOT / "fw2/ui" / f).read_text(); mut = txt.replace(old, new)
        assert mut != txt, "%s: the control has nothing to change (%r)" % (f, old)
        (T / "fw2/ui" / f).unlink(); (T / "fw2/ui" / f).write_text(mut)
    real = pm.ROOT; pm.ROOT = T
    try: blob, _ = pm.build("layered_wave", SIM_ORG, tmp / (name + ".elf"), SIMREGS, scratch=SIM_SCRATCH)
    finally: pm.ROOT = real
    return blob

def main():
    with tempfile.TemporaryDirectory() as d:
        tmp = Path(d)
        blob, info = pm.build("layered_wave", SIM_ORG, tmp / "lw.elf", SIMREGS, scratch=SIM_SCRATCH)
        res = sweep(tmp, blob, range(N_PRESETS))
        for p, (i, m) in enumerate(res): print("     preset %2d: %9.0f instructions, %6.0f misses per tick, %6.2f M modelled cycles" % (p, i, m, (i * 1.65 + m * 253) / 1e6))
        worst_m = max(m for _, m in res); worst_c = max(i * 1.65 + m * 253 for i, m in res)
        check("every preset stays under %d modelled instruction-cache misses per tick (worst %.0f)" % (BUDGET_MISSES, worst_m), worst_m <= BUDGET_MISSES)
        check("every preset stays under %.1f M modelled cycles per tick (worst %.2f M)" % (BUDGET_CYCLES / 1e6, worst_c / 1e6), worst_c <= BUDGET_CYCLES)
        for name, what, edits in (("nonoinline", "no noinline on the view functions (the layout of the old player)", [("layered_wave.inc", "static __attribute__((noinline)) void lw_", "static void lw_")]),
                                  ("noplace", "no section placement (noinline only, the DEV 205 test pack)", [("meter_layered_wave.c", "#ifdef P2_PACK                                            /* code placement", "#if 0                                                     /* code placement")])):
            mres = sweep(tmp, mutant_blob(tmp, name, edits), range(N_PRESETS))
            mw = max(m for _, m in mres)
            print("     control (%s): worst %.0f misses per tick" % (what, mw))
            check("control: %s breaks the miss budget (the test sees the problem)" % what, mw > BUDGET_MISSES)
    print("FAILED" if fails else "PASSED"); sys.exit(1 if fails else 0)
main()
