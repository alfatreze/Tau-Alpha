#!/usr/bin/env python3
"""Layered Wave as a pack against the built-in meter: instructions per picture under tools/rv32sim.py (docs/features/meters/METER_PACKS_PLAYER2.md, "Pack cost").

Both run the same input trace (sim/p2_pack_trace.h, -DTR_NOHASH so an engine call costs the same few instructions either way): the built-in compiled straight in (tools/host/p2_builtin_harness.c) and the pack loaded
through the firmware's loader and called through the host table (tools/host/p2_pack_harness.c). The per-picture cost is the marginal one, (run of 90 frames - run of 10 frames) / frames, so the trace generator and the
loader drop out. What the instruction count cannot say is the fetch cost: on the Pocket the built-in is a cold object (fw2/platform/link.ld: meter_layered_wave.o) and a pack is a slot; BOTH execute from PSRAM through the
instruction alias, so they pay the same fetch price per instruction; so the pack's relative cost in instructions carries over to cycles. The absolute cycles are the Pocket's to give: its Info page (METER MAX LAYERED WAVE, a build with and
one without PACKS=1) is the measurement; this script bounds what the pack adds.
  python3 tools/player2/measure_pack_cost.py"""
import re, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools/player2")); sys.path.insert(0, str(ROOT / "tools"))
import pack_meter as pm
SIM_ORG, SIM_SCRATCH = 0x00400000, 0x00300000
SIMREGS = ["-include", str(ROOT / "sim/p2_pack_sim_regs.h")]
def gcc(out, src, extra):
    cmd = [pm.tool("gcc"), "-march=rv32im", "-mabi=ilp32", "-mno-relax", "-O2", "-ffreestanding", "-nostdlib", "-nostartfiles", "-Wall", "-Wno-unused-function", "-Wno-unused-variable", "-Wno-unused-const-variable", "-Wno-comment",
           "-DPACK_LW", "-DTR_NOHASH"] + extra + ["-Wl,--no-warn-rwx-segments", "-T", str(ROOT / "tools/host/link.ld"), str(ROOT / "tools/host/start.S"), str(src),
           "-I", str(ROOT / "fw2/ui"), "-I", str(ROOT / "fw2/cores"), "-I", str(ROOT / "fw"), "-o", str(out), "-lgcc"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode: sys.exit(r.stderr)
def sim(elf, blob=None):
    args = [sys.executable, str(ROOT / "tools/rv32sim.py"), "--dev=" + str(ROOT / "tools/p2_dev_model.py"), str(elf)] + ([str(blob)] if blob else [])
    r = subprocess.run(args, capture_output=True, text=True)
    n = re.search(r"\[([\d,]+) instructions", r.stderr)
    if not n: sys.exit("no instruction count:\n" + r.stderr + r.stdout)
    sc = [l.split() for l in r.stdout.splitlines() if l.startswith("S ")]
    return int(n.group(1).replace(",", "")), sum(int(l[2]) for l in sc), len(sc)
def main():
    with tempfile.TemporaryDirectory() as d:
        tmp = Path(d)
        blob, info = pm.build("layered_wave", SIM_ORG, tmp / "lw.elf", SIMREGS, scratch=SIM_SCRATCH)
        (tmp / "lw.tmpk").write_bytes(blob)
        res = {}
        for frames in (10, 90):
            for kind in ("pack", "builtin"):
                elf = tmp / ("%s_%d.elf" % (kind, frames))
                if kind == "pack":
                    gcc(elf, ROOT / "tools/host/p2_pack_harness.c", ["-DPACK_ORG=0x%X" % SIM_ORG, "-DSCRATCH_ORG=0x%X" % SIM_SCRATCH, "-DPACK_METER=%d" % pm.METER_IDS["layered_wave"], "-DPACK_SLOT=%d" % pm.SLOT["layered_wave"], "-DTR_NFRAMES=%d" % frames])
                    res[kind, frames] = sim(elf, tmp / "lw.tmpk")
                else:
                    gcc(elf, ROOT / "tools/host/p2_builtin_harness.c", SIMREGS + ["-DTR_NFRAMES=%d" % frames])
                    res[kind, frames] = sim(elf)
        out = {}
        for kind in ("pack", "builtin"):
            (i10, c10, n10), (i90, c90, n90) = res[kind, 10], res[kind, 90]
            out[kind] = (i90 - i10, c90 - c10, (90 - 10) * n90)
        print("Layered Wave, marginal instructions over the same trace (%d pictures, %d draw commands):" % (out["pack"][2], out["pack"][1]))
        for kind in ("builtin", "pack"): print("  %-8s %10d instructions   %8.0f per picture   %6.1f per draw command" % (kind, out[kind][0], out[kind][0] / out[kind][2], out[kind][0] / out[kind][1]))
        if out["builtin"][1] != out["pack"][1]: print("WARNING: the two runs drew different numbers of commands")
        d = out["pack"][0] - out["builtin"][0]
        print("  the pack adds %d instructions (+%.2f%%), %.1f per draw command: the host-table indirection and the command counter" % (d, 100.0 * d / out["builtin"][0], d / out["pack"][1]))
main()
