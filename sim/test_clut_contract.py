#!/usr/bin/env python3
"""B-570: the firmware's CLUT start index must match what the RTL does with a write.

mp3_soc.v (as of B-569) raises a one-cycle write pulse and advances clut_idx on the same edge while `clut_waddr` follows
`clut_idx`, so a DATA write lands one slot above the index it was issued at. Firmware compensates by starting a load at
R_CLUT_IDX = 255 (CLUT_START_IDX, fw/player.c). When the RTL registers the write address with the pulse, the constant must become
0 in the same change (and CORE_VERSION bump); this test fails if the RTL and the constants disagree, and if any loader bypasses the
constant. The host model in sim/timg_harness.c uses the same two numbers."""
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
if re.search(r"assign\s+clut_waddr\s*=\s*clut_idx\s*;", rtl):
    skewed = True
elif re.search(r"assign\s+clut_waddr\s*=\s*clut_waddr_r\s*;", rtl):
    skewed = False
else:
    print("FAIL cannot tell how mp3_soc.v drives clut_waddr: update this test with the new shape")
    sys.exit(1)
expect_start, expect_skew = (255, 1) if skewed else (0, 0)
print(f"RTL: clut_waddr {'follows clut_idx (write lands one slot high)' if skewed else 'is registered with the pulse'}")

player = (ROOT / "fw/player.c").read_text()
m = re.search(r"#define\s+CLUT_START_IDX\s+(\d+)u", player)
check(m and int(m[1]) == expect_start, f"fw/player.c CLUT_START_IDX == {expect_start}")
h = (ROOT / "sim/timg_harness.c").read_text()
m = re.search(r"#define\s+CLUT_START_IDX\s+(\d+)u", h)
check(m and int(m[1]) == expect_start, f"sim/timg_harness.c default CLUT_START_IDX == {expect_start}")
m = re.search(r"#define\s+CLUT_SKEW\s+(\d+)u", h)
check(m and int(m[1]) == expect_skew, f"sim/timg_harness.c default CLUT_SKEW == {expect_skew}")

bad = []
for f in sorted((ROOT / "fw").glob("*.c")) + sorted((ROOT / "fw").glob("*.inc")):
    for n, line in enumerate(f.read_text().splitlines(), 1):
        if re.search(r"REG\(R_CLUT_IDX\)\s*=", line) and "CLUT_START_IDX" not in line:
            bad.append(f"{f.name}:{n}")
check(not bad, "every firmware CLUT load starts at CLUT_START_IDX" + (f" (bypassed at {bad})" if bad else ""))
print("PASSED" if not fails else f"{fails} FAILED")
sys.exit(1 if fails else 0)
