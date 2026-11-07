#!/usr/bin/env python3
"""Instruction-cache sweep over the packs (docs/features/LAYERED_WAVE_PERF_PLAN.md, section 2: "Chladni is the next candidate to look at, cheaply, with the same tool").

The CPU runs pack code from PSRAM through a 4 KiB direct-mapped instruction cache (128 lines of 32 bytes); each miss costs about 253 cycles [MODEL, docs/PHASE_G_SPEC.md]. For every pack with a trace in
sim/p2_pack_trace.h (Winamp Bars, Winamp Scope, Master VU, Chladni, Layered Wave, Waterfall Grid; Sound Map has no shared trace) this runs the pack as shipped (linked at the simulator slot, 4 KiB aligned, so the cache
indices equal the Pocket's) through a copy of tools/rv32sim.py that models the cache, and prints per picture: instructions, misses, the modelled cycles (instructions x 1.65 + misses x 253), the share of the
cycles that are misses, and the worst CONFLICTS: pairs of functions whose lines evict each other, with how often (per picture). The marginal cost is (13 frames - 3 frames) / 10 so the load drops out.
  python3 tools/player2/icache_sweep.py [KEY ...] [--top N]
Needs the RISC-V toolchain (RISCV_TOOLCHAIN_BIN, or toolchain/ in the tree). Host-simulated only: the constants are not validated on a Pocket."""
import collections, contextlib, importlib.util, io, re, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools/player2")); sys.path.insert(0, str(ROOT / "tools"))
import pack_meter as pm
SIM_ORG, SIM_SCRATCH = 0x00400000, 0x00300000
SIMREGS = ["-include", str(ROOT / "sim/p2_pack_sim_regs.h")]
FLAG = {"winamp_bars": "PACK_BARS", "winamp_scope": "PACK_SCOPE", "vu_master": "PACK_VU", "chladni": "PACK_CHL", "layered_wave": "PACK_LW", "waterfall_grid": "PACK_WF"}
CYC_INSTR, CYC_MISS = 1.65, 253

_src = (ROOT / "tools/rv32sim.py").read_text()
_old = "        while n < max_instr:\n            n += 1\n"
assert _old in _src, "tools/rv32sim.py main loop changed: update the cache hook"
_src = _src.replace(_old, "        T = self.T\n        while n < max_instr:\n            n += 1\n            if pc >= 0x400000:\n                ln = pc >> 5; ix = ln & 127\n                if T[ix] != ln:\n                    self.ev[(T[ix], ln)] += 1; T[ix] = ln; self.miss += 1\n", 1)
_ns = {"__name__": "rv32cache"}; exec(compile(_src, "rv32cache", "exec"), _ns)
_sp = importlib.util.spec_from_file_location("devmodel", ROOT / "tools/p2_dev_model.py")

def sim(elf, blob):
    m = _ns["Machine"](); m.T = [-1] * 128; m.miss = 0; m.ev = collections.Counter()
    m.dev = importlib.util.module_from_spec(_sp); _sp.loader.exec_module(m.dev)
    m.load_elf(str(elf)); m.blob = blob
    m.run()
    return m.icount, m.miss, m.ev

def harness(tmp, key, frames, tag):
    elf = tmp / ("h_%s_%s_%d.elf" % (key, tag, frames))
    cmd = [pm.tool("gcc"), "-march=rv32im", "-mabi=ilp32", "-mno-relax", "-O2", "-ffreestanding", "-nostdlib", "-nostartfiles", "-w"] + SIMREGS + [
           "-D" + FLAG[key], "-DTR_NOHASH", "-DTR_NFRAMES=%d" % frames, "-DPACK_ORG=0x%X" % SIM_ORG, "-DSCRATCH_ORG=0x%X" % SIM_SCRATCH, "-DPACK_METER=%d" % pm.METER_IDS[key], "-DPACK_SLOT=%d" % pm.SLOT[key],
           "-Wl,--no-warn-rwx-segments", "-T", str(ROOT / "tools/host/link.ld"), str(ROOT / "tools/host/start.S"), str(ROOT / "tools/host/p2_pack_harness.c"),
           "-I", str(ROOT / "fw2/ui"), "-I", str(ROOT / "fw2/cores"), "-I", str(ROOT / "fw"), "-I", str(ROOT / "tools/host"), "-o", str(elf), "-lgcc"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode: sys.exit(key + ": " + r.stderr[-600:])
    return elf

def symbols(elf):
    """(address, size, name) of the functions of the pack's ELF, for naming a cache line"""
    out = subprocess.run([pm.tool("nm"), "-S", "--defined-only", str(elf)], capture_output=True, text=True).stdout
    syms = []
    for ln in out.splitlines():
        p = ln.split()
        if len(p) == 4 and p[2] in "tTwW": syms.append((int(p[0], 16), int(p[1], 16), p[3]))
    return sorted(syms)

def name_of(syms, line):
    a = line << 5
    for s, z, n in syms:
        if s <= a + 31 and a < s + max(z, 1): return n
    return "?"

def sweep_one(tmp, key, top):
    blob, info = pm.build(key, SIM_ORG, tmp / (key + ".elf"), SIMREGS, scratch=SIM_SCRATCH)
    (tmp / (key + ".tmpk")).write_bytes(blob)
    a = sim(harness(tmp, key, 13, "a"), blob); b = sim(harness(tmp, key, 3, "b"), blob)
    n = 10.0
    ev = collections.Counter({k: (a[2][k] - b[2][k]) for k in a[2] if a[2][k] - b[2][k] > 0})
    syms = symbols(tmp / (key + ".elf"))
    pairs = collections.Counter()
    for (old, new), c in ev.items():
        if old == -1: continue
        x, y = sorted((name_of(syms, old), name_of(syms, new)))
        pairs[(x, y)] += c
    instr, miss = (a[0] - b[0]) / n, (a[1] - b[1]) / n
    cyc = instr * CYC_INSTR + miss * CYC_MISS
    return {"key": key, "instr": instr, "miss": miss, "cyc": cyc, "share": miss * CYC_MISS / cyc if cyc else 0.0, "pairs": [(p, c / n) for p, c in pairs.most_common(top)]}

def main():
    args = [x for x in sys.argv[1:] if not x.startswith("--")]; top = 5
    if "--top" in sys.argv: top = int(sys.argv[sys.argv.index("--top") + 1]); args = [x for x in args if x != str(top)]
    keys = args or list(FLAG)
    bad = [k for k in keys if k not in FLAG]
    if bad: sys.exit("no shared trace for %s (have: %s)" % (", ".join(bad), ", ".join(FLAG)))
    with tempfile.TemporaryDirectory() as d:
        tmp = Path(d)
        with contextlib.redirect_stdout(io.StringIO()), ThreadPoolExecutor(max_workers=3) as ex:        # the simulated harness prints its own trace
            res = list(ex.map(lambda k: sweep_one(tmp, k, top), keys))
    print("%-16s %12s %10s %12s %8s" % ("pack", "instr/pic", "miss/pic", "M cycles", "miss %"))
    for r in res:
        print("%-16s %12.0f %10.0f %12.3f %7.1f%%" % (r["key"], r["instr"], r["miss"], r["cyc"] / 1e6, 100 * r["share"]))
    for r in res:
        if r["pairs"]:
            print("\n%s: functions that evict each other (misses per picture)" % r["key"])
            for (x, y), c in r["pairs"]: print("   %7.1f  %s  <->  %s" % (c, x, y))
main()
