#!/usr/bin/env python3
"""Instructions per picture of each Waterfall preset as a pack under tools/rv32sim.py (the pack runs from PSRAM on the Pocket at roughly 28 cycles an instruction: 1 M instructions is about 0.4 s of CPU per second of music at 40 pictures a second... use the printed per-picture figure x 28 for cycles). Marginal: (90 - 10 frames) per scenario, same input trace for every preset.
  python3 tools/player2/measure_wf_modes.py"""
import re, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools/player2")); sys.path.insert(0, str(ROOT / "tools"))
import pack_meter as pm
SIM_ORG, SIM_SCRATCH = 0x00400000, 0x00300000
SIMREGS = ["-include", str(ROOT / "sim/p2_pack_sim_regs.h")]
def build(out, extra):
    cmd = [pm.tool("gcc"), "-march=rv32im", "-mabi=ilp32", "-mno-relax", "-O2", "-ffreestanding", "-nostdlib", "-nostartfiles", "-w", "-DPACK_WF", "-DTR_NOHASH", "-DPACK_ORG=0x%X" % SIM_ORG, "-DSCRATCH_ORG=0x%X" % SIM_SCRATCH,
           "-DPACK_METER=%d" % pm.METER_IDS["waterfall_grid"], "-DPACK_SLOT=%d" % pm.SLOT["waterfall_grid"]] + extra + ["-Wl,--no-warn-rwx-segments", "-T", str(ROOT / "tools/host/link.ld"), str(ROOT / "tools/host/start.S"), str(ROOT / "tools/host/p2_pack_harness.c"),
           "-I", str(ROOT / "fw2/ui"), "-I", str(ROOT / "fw2/cores"), "-I", str(ROOT / "fw"), "-o", str(out), "-lgcc"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode: sys.exit(r.stderr)
def instr(elf, blob):
    r = subprocess.run([sys.executable, str(ROOT / "tools/rv32sim.py"), "--dev=" + str(ROOT / "tools/p2_dev_model.py"), str(elf), str(blob)], capture_output=True, text=True)
    m = re.search(r"\[([\d,]+) instructions", r.stderr); return int(m.group(1).replace(",", ""))
with tempfile.TemporaryDirectory() as d:
    d = Path(d); blob, _ = pm.build("waterfall_grid", SIM_ORG, d / "wf.elf", SIMREGS, scratch=SIM_SCRATCH); (d / "wf.tmpk").write_bytes(blob)
    names = re.findall(r'"([A-Z ]+)"', re.search(r"mpp_wf_name\[[^\]]*\] = \{([^}]*)\}", (ROOT / "fw2/generated/meter_presets.h").read_text()).group(1))
    for s, name in enumerate(names):
        r = {}
        for fr in (10, 90):
            elf = d / ("h_%d_%d.elf" % (s, fr)); build(elf, ["-DTR_WF_ONLY=%d" % s, "-DTR_NFRAMES=%d" % fr]); r[fr] = instr(elf, d / "wf.tmpk")
        per = (r[90] - r[10]) / (2 * 80)
        print("%-10s %9.0f instructions per picture  (x28 = %.1f ms at 66.7 MHz from PSRAM)" % (name, per, per * 28 / 66666.0))
