#!/usr/bin/env python3
"""The diagnostic stress pump must not touch memory the player is using (B-590). It once ran over [2 MiB, 3 MiB), which is exactly where the second
display buffer of the H2 double buffer sits (mp3_fb.sv DBUF_BASE1 = 1,048,576 16-bit words = 2 MiB): during Check's Stress R1-R3 the screen filled with the
pump's random data. Parses the real constants and refuses any overlap between the pump range and (a) buffer 1's visible area, (b) the playlist/queue
buffers at PL_SDRAM_BASE, (c) buffer 0's visible area and its off-screen stash rows."""
import re, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
def num(s): return int(s.rstrip("uU"), 0)
fb = (ROOT / "src/fpga/core/mp3_fb.sv").read_text()
dbuf_base1 = int(re.search(r"DBUF_BASE1\s*=\s*25'd(\d+)", fb).group(1))
vis = int(re.search(r"DBUF_VISIBLE_WORDS\s*=\s*19'd(\d+)", fb).group(1))
sd = (ROOT / "fw/stress_defs.inc").read_text()
base = num(re.search(r"#define STRESS_BASE\s+(\S+)", sd).group(1))
last = num(re.search(r"#define STRESS_LAST\s+(\S+)", sd).group(1))
pl = num(re.search(r"#define PL_SDRAM_BASE\s+(\S+)", (ROOT / "fw/playlist.inc").read_text()).group(1)) - 0xA0000000   # byte offset
# the stash rows packed above the visible area: the highest in use today is row 1023 of a 512-word stride (mp3_fb.sv comment)
stash_end = 1024 * 512
regions = {
    "display buffer 0 + stash (words)": (0, stash_end),
    "display buffer 1 visible (words)": (dbuf_base1, dbuf_base1 + vis),
    "playlist buffers from PL_SDRAM_BASE (words, 1 MiB)": (pl // 2, pl // 2 + 0x80000),
}
fails = 0
print(f"stress pump words [{base:#x}, {last:#x}]")
for name, (a, b) in regions.items():
    ok = last < a or base >= b
    print(("ok   " if ok else "FAIL ") + f"pump range does not overlap {name} [{a:#x}, {b:#x})")
    fails += 0 if ok else 1
print("PASSED" if not fails else f"{fails} FAILED")
sys.exit(1 if fails else 0)
