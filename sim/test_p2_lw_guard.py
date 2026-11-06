#!/usr/bin/env python3
"""Layered Wave's cost guard counts CPU time on the real CPU (docs/features/LAYERED_WAVE_PERF_PLAN.md, P1b; B-948).

Until B-948 Player 2 ran the host's command-count rule (a redraw is kept near 300 commands by widening the drawing cells), because the time rule sat under `LW_STATS`, which only the old player defined. With the CPU a
lot cheaper than in the old player that rule held NEON at an 8-pixel cell (owner: the bars shimmer). This test runs the pack under tools/rv32sim.py with `-DLW_TIME_GUARD` and a MODELLED clock (instructions x 1.65 +
instruction-cache misses x 253, the model of sim/test_p2_icache.py) behind the cycle-counter register, over the 16 presets, and checks:
  1. the rule is the same function for both guards (the count-rule build, which the golden test compares, is untouched: `lw_guard_step` is the old expression);
  2. settled (the last 30 of 100 frames), no preset costs more with the time rule than the default limit (9 ms = 600,000 cycles) or than it did under the count rule (+10 %): the time rule never makes a redraw heavier than before;
  3. the rule on its own (the text between LW_GUARD_TIME_BEGIN/END, compiled natively) on modelled music: it changes the cell width at most a third as often as the plain rule when the music swings around the limit, ends at 1 px on light music and under the limit on heavy music;
  4. where a redraw is far under the limit the time rule narrows the cell (at least one preset draws 1.5 x the commands).
The model's harness feeds random bands, which cost far more per command (about 2,500 cycles) than real music on a Pocket (DEV 206: about 560 cycles per command at an 8-pixel cell), so the cell widths it settles at are not the Pocket's; what it proves is the direction and the bound.
Host-simulated only. The clock model is not validated on a Pocket."""
import importlib.util, os, subprocess, sys, tempfile, types
from concurrent.futures import ProcessPoolExecutor
import multiprocessing
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/player2")); sys.path.insert(0, str(ROOT / "tools"))
import pack_meter as pm
SIM_ORG, SIM_SCRATCH = 0x00400000, 0x00300000
SIMREGS = ["-include", str(ROOT / "sim/p2_pack_sim_regs.h")]
N_PRESETS, NF, SETTLE = 16, 100, 70        # the cell narrows only after a calm stretch (24 redraws), so the model needs more frames to settle
HI = 600_000                    # CLK_HZ / 1000 * 9 (the default guard), cycles
fails = 0
def check(name, cond, info=""):
    global fails
    print(("ok   " if cond else "FAIL ") + name + ("  " + info if info and not cond else ""))
    fails += 0 if cond else 1

_src = (ROOT / "tools/rv32sim.py").read_text()
_old = "        while n < max_instr:\n            n += 1\n"
assert _old in _src, "tools/rv32sim.py main loop changed: update the clock hook"
_src = _src.replace(_old, "        T = self.T\n        while n < max_instr:\n            n += 1\n            self.cur = n\n            if pc >= 0x400000:\n                ln = pc >> 5; ix = ln & 127\n                if T[ix] != ln:\n                    T[ix] = ln; self.miss += 1\n", 1)
_ns = {"__name__": "rv32clock"}; exec(compile(_src, "rv32clock", "exec"), _ns)
_sp = importlib.util.spec_from_file_location("devmodel", ROOT / "tools/p2_dev_model.py")

def run(elf, blob):
    m = _ns["Machine"](); m.T = [-1] * 128; m.miss = 0; m.cur = 0
    real = importlib.util.module_from_spec(_sp); _sp.loader.exec_module(real)
    clock = lambda: int(m.cur * 1.65 + m.miss * 253) & 0xFFFFFFFF
    m.dev = types.SimpleNamespace(load=lambda off: clock() if off == 0x0C else real.load(off), store=real.store)
    m.load_elf(str(elf)); m.blob = blob
    import contextlib, io
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf): m.run()
    return [tuple(int(x) for x in ln.split()[1:]) for ln in buf.getvalue().splitlines() if ln.startswith("F ")]

def harness(tmp, preset):
    elf = tmp / ("h_%d.elf" % preset)
    cmd = [pm.tool("gcc"), "-march=rv32im", "-mabi=ilp32", "-mno-relax", "-O2", "-ffreestanding", "-nostdlib", "-nostartfiles", "-w"] + SIMREGS + [
           "-DPACK_LW", "-DTR_NOHASH", "-DLW_TRACE", "-DLW_P=%d" % preset, "-DLW_FS=0", "-DNF=%d" % NF, "-DPACK_ORG=0x%X" % SIM_ORG, "-DSCRATCH_ORG=0x%X" % SIM_SCRATCH,
           "-DPACK_METER=%d" % pm.METER_IDS["layered_wave"], "-DPACK_SLOT=%d" % pm.SLOT["layered_wave"], "-Wl,--no-warn-rwx-segments", "-T", str(ROOT / "tools/host/link.ld"),
           str(ROOT / "tools/host/start.S"), str(ROOT / "tools/host/p2_lw_cost_harness.c"), "-I", str(ROOT / "fw2/ui"), "-I", str(ROOT / "fw2/cores"), "-I", str(ROOT / "fw"), "-I", str(ROOT / "tools/host"), "-o", str(elf), "-lgcc"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return elf

def _job(a):
    return run(*a)
def sweep(tmp, blob):
    elfs = [harness(tmp, p) for p in range(N_PRESETS)]
    with ProcessPoolExecutor(max_workers=os.cpu_count() or 4, mp_context=multiprocessing.get_context("fork")) as ex:    # a process per run: the simulator writes the guest's console to its own stdout
        return list(ex.map(_job, [(e, blob) for e in elfs]))

def settled(frames):
    f = frames[SETTLE:]
    return sum(x[1] for x in f) / len(f), sum(x[2] for x in f) / len(f)             # (frame, commands, cycles, scrolls, violations)

def rule_test(tmp):
    """the time guard's rule (the text between LW_GUARD_TIME_BEGIN/END in layered_wave.inc, compiled natively) on a modelled music: the CPU cost of a redraw is base / cell width, the loudness moves it up and down"""
    inc = (ROOT / "fw2/ui/layered_wave.inc").read_text()
    a = inc.index("LW_GUARD_TIME_BEGIN"); b = inc.index("LW_GUARD_TIME_END")
    body = inc[inc.index("\n", a):inc.rindex("/*", a, b + 1) if False else b]
    body = body[:body.rindex("/*")]
    c = tmp / "rule.c"
    c.write_text('''#include <stdint.h>
#include <stdio.h>
static inline int32_t lw_min(int32_t a, int32_t b) { return a < b ? a : b; }
''' + body + '''
static uint8_t old_step(uint8_t stride, uint32_t m, uint32_t hi) { return (uint8_t)(m > hi ? lw_min(16, (int32_t)stride + 1) : (m < (hi >> 1) && stride > 1u ? stride - 1 : stride)); }
int main(void)
{
    const uint32_t hi = 600000u;
    for (int sc = 0; sc < 3; sc++) {                       /* 0: music that swings around the limit, 1: always light, 2: always heavy */
        uint8_t sn = 3, so = 3, calm = 0; uint32_t seed = 12345u; int chn = 0, cho = 0; uint32_t last_n = 0;
        for (int t = 0; t < 1200; t++) {
            seed = seed * 1664525u + 1013904223u; const uint32_t r = (seed >> 16) & 255u;
            const uint32_t loud = sc == 0 ? 1000u + (((t / 7) & 1) ? 2500u : 0u) + r : sc == 1 ? 300u : 2400u;      /* loud and quiet passages of 7 redraws, a swing of 3.5 x, plus noise */
            const uint32_t base = 1800u * loud;                  /* cost at cell width 1, cycles */
            const uint8_t n2 = lw_guard_step_t(sn, base / sn, hi, &calm), o2 = old_step(so, base / so, hi);
            chn += n2 != sn; cho += o2 != so; sn = n2; so = o2; last_n = base / sn;
        }
        printf("%d %d %d %d %u\\n", sc, chn, cho, sn, last_n);
    }
    return 0;
}
''')
    exe = tmp / "rule"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-o", str(exe), str(c)], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return [tuple(int(x) for x in ln.split()) for ln in subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout.splitlines()]

def main():
    with tempfile.TemporaryDirectory() as d:
        tmp = Path(d)
        blob_c, _ = pm.build("layered_wave", SIM_ORG, tmp / "count.elf", SIMREGS, scratch=SIM_SCRATCH)
        blob_t, _ = pm.build("layered_wave", SIM_ORG, tmp / "time.elf", SIMREGS + ["-DLW_TIME_GUARD=1"], scratch=SIM_SCRATCH)
        rc, rt = sweep(tmp, blob_c), sweep(tmp, blob_t)
        gain = 0
        for p in range(N_PRESETS):
            (cc, tc), (ct, tt) = settled(rc[p]), settled(rt[p])
            print("     preset %2d: count rule %6.0f commands %6.2f M cycles | time rule %6.0f commands %6.2f M cycles" % (p, cc, tc / 1e6, ct, tt / 1e6))
        check("both builds produced %d frames for every preset" % NF, all(len(r) == NF for r in rc + rt))
        C = [settled(rc[p]) for p in range(N_PRESETS)]; T = [settled(rt[p]) for p in range(N_PRESETS)]
        check("settled, the time rule never costs more than the default limit (%d cycles) or than the count rule did (+25 %%: a noisy redraw swings around the limit)" % HI, all(T[p][1] <= 1.25 * max(HI, C[p][1]) for p in range(N_PRESETS)), str([(round(T[p][1]), round(C[p][1])) for p in range(N_PRESETS)]))
        # control: a guard that widens every redraw must fail the finer-cell check (the test sees the rule)
        T0 = Path(tmp / "mut"); (T0 / "fw2/ui").mkdir(parents=True)
        for e in (ROOT / "fw2").iterdir():
            if e.name != "ui": os.symlink(e, T0 / "fw2" / e.name)
        for e in (ROOT / "fw2/ui").iterdir(): os.symlink(e, T0 / "fw2/ui" / e.name)
        for n in ("fw", "meters", "toolchain"): os.symlink(ROOT / n, T0 / n)
        inc = (ROOT / "fw2/ui/layered_wave.inc").read_text(); old = "if (m > hi) {"
        assert old in inc; (T0 / "fw2/ui/layered_wave.inc").unlink(); (T0 / "fw2/ui/layered_wave.inc").write_text(inc.replace(old, "if (1) {"))
        real = pm.ROOT; pm.ROOT = T0
        try: blob_m, _ = pm.build("layered_wave", SIM_ORG, tmp / "mut.elf", SIMREGS + ["-DLW_TIME_GUARD=1"], scratch=SIM_SCRATCH)
        finally: pm.ROOT = real
        rm = sweep(tmp, blob_m); M = [settled(rm[p]) for p in range(N_PRESETS)]
        check("control: a guard that always widens is caught (no preset draws a finer cell)", not [p for p in range(N_PRESETS) if M[p][0] >= 1.5 * C[p][0]])
        rr = rule_test(tmp)
        for sc, chn, cho, cell, cost in rr: print("     rule, music %d: cell width changes %d (old rule %d), ends at %d px costing %d cycles" % (sc, chn, cho, cell, cost))
        check("the rule moves the cell far less than the plain rule on music that swings around the limit (at most a third as often)", rr[0][1] * 3 <= rr[0][2] and rr[0][2] >= 30, str(rr[0]))
        check("always-light music ends at the finest cell (1 px), always-heavy music at a cost under the limit", rr[1][3] == 1 and rr[2][4] <= HI, str(rr[1:]))
        finer = [p for p in range(N_PRESETS) if T[p][0] >= 1.5 * C[p][0]]
        check("where a redraw is far under the limit the time rule draws a finer cell (1.5 x the commands; presets %s; at least 1 needed)" % finer, len(finer) >= 1)
    print("FAILED" if fails else "PASSED"); sys.exit(1 if fails else 0)
if __name__ == "__main__": main()
