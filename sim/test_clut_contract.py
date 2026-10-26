#!/usr/bin/env python3
"""B-570/B-575: the pieces of the CLUT write fix must stay consistent.

  - RTL: mp3_soc.v must drive the CLUT write port through tau_clut_wr (registered write address, own testbench) and must not go back
    to the inline version where `clut_waddr` followed the already-advanced `clut_idx` (the one-slot skew of B-569).
  - Firmware: the start index is probed once (clut_probe(), fw/blit_probe.inc, run from blit_probe_ensure()); until the probe has run, and
    if it cannot tell, the LEGACY start 255 is used, because every bitstream built before B-575 has the skew. The default must be 255.
  - Every firmware CLUT load starts at CLUT_START_IDX (never a literal), and the probe is called from blit_probe_ensure().
  - The host models (sim/timg_harness.c, sim/thumb_dbuf_harness.c) default to the legacy RTL (CLUT_SKEW 1, start 255)."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
fails = 0


def check(cond, msg):
    global fails
    print(("ok   " if cond else "FAIL ") + msg)
    fails += not cond


rtl = (ROOT / "src/fpga/core/mp3_soc.v").read_text()
check(re.search(r"\btau_clut_wr\s+u_clut_wr\b", rtl) is not None, "mp3_soc.v instantiates tau_clut_wr for the CLUT write port")
check(re.search(r"assign\s+clut_waddr\s*=\s*clut_idx\s*;", rtl) is None, "mp3_soc.v does not drive clut_waddr from the live index again")
mod = (ROOT / "src/fpga/core/tau_clut_wr.sv").read_text()
check(re.search(r"waddr_r\s*<=\s*idx\s*;", mod) is not None, "tau_clut_wr registers the address with the write pulse")
check("tau_clut_wr.sv" in (ROOT / "src/fpga/ap_core.qsf").read_text(), "tau_clut_wr.sv is in ap_core.qsf")

player = (ROOT / "fw/player.c").read_text()
check(re.search(r"static uint8_t clut_start_idx_v\s*=\s*255u;", player) is not None, "firmware default CLUT start is the legacy 255")
check(re.search(r"#define\s+CLUT_START_IDX\s+\(\(uint32_t\)clut_start_idx_v\)", player) is not None, "CLUT_START_IDX is the probed variable")
probe = (ROOT / "fw/blit_probe.inc").read_text()
check(re.search(r"if \(blit_ready && !clut_probed\) clut_probe\(\);", probe) is not None, "blit_probe_ensure() runs the CLUT probe once the blit engine is known")
check(re.search(r"clut_start_idx_v = 0u;", probe) and re.search(r"clut_start_idx_v = 255u;", probe), "the probe can choose both start indices")

for name, want in (("sim/timg_harness.c", ("CLUT_SKEW", "1u")), ):
    h = (ROOT / name).read_text()
    m = re.search(r"#define\s+CLUT_SKEW\s+(\d+u)", h)
    check(m and m[1] == "1u", f"{name} defaults to the legacy RTL (CLUT_SKEW 1)")
h = (ROOT / "sim/timg_harness.c").read_text()
m = re.search(r"#define\s+CLUT_START_IDX\s+(\d+u)", h)
check(m and m[1] == "255u", "sim/timg_harness.c defaults to start 255")

bad = []
for f in sorted((ROOT / "fw").glob("*.c")) + sorted((ROOT / "fw").glob("*.inc")):
    for n, line in enumerate(f.read_text().splitlines(), 1):
        if re.search(r"REG\(R_CLUT_IDX\)\s*=", line) and "CLUT_START_IDX" not in line and not (f.name == "blit_probe.inc" and "= 0u" in line):
            bad.append(f"{f.name}:{n}")
check(not bad, "every firmware CLUT load starts at CLUT_START_IDX (the probe's own load starts at 0 on purpose)" + (f" (bypassed at {bad})" if bad else ""))
print("PASSED" if not fails else f"{fails} FAILED")
sys.exit(1 if fails else 0)
