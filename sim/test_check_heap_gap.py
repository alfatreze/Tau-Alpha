#!/usr/bin/env python3
"""D-T20, rule 6 of docs/PROJECT_RULES.md: tools/check_heap_gap.py measures Player 2 (fw2/build.sh, DIAG=0 and DIAG=1: the one shipped 192 KB / CLK66 link).
Unit test of its pure parts, without a build: the heap gap read from `nm` output (the linker's _heap_limit - _image_end, and the _min_free floor the link itself asserts)
and the verdict against the recorded baseline."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
fails = 0
def check(msg, cond):
    global fails
    print(("ok   " if cond else "FAIL ") + msg)
    fails += not cond

import check_heap_gap as h

NM = """00027c00 A _heap_limit
00025370 B _image_end
00000400 A _min_free
00030000 B _stack_top
00027c00 A _tag_start
"""
gap, floor = h.gap_from_nm(NM)
check("gap = _heap_limit - _image_end = 0x27c00 - 0x25370", gap == 0x27c00 - 0x25370 and gap == 10384)
check("floor = _min_free = 1024", floor == 1024)
try:
    h.gap_from_nm("00027c00 A _heap_limit\n"); missing = False
except SystemExit:
    missing = True
check("nm output without _image_end is refused", missing)
check("a gap that fell more than the tolerance below the baseline is a failure", h.verdict("d1", 10384 - h.TOLERANCE_B - 1, 1024, {"d1": 10384})[0] is False)
check("a gap within the tolerance, or higher, passes", h.verdict("d1", 10384 - h.TOLERANCE_B, 1024, {"d1": 10384})[0] and h.verdict("d1", 20000, 1024, {"d1": 10384})[0])
check("a gap under the link floor fails whatever the baseline says", h.verdict("d1", 1000, 1024, {"d1": 900})[0] is False)
check("no baseline entry: reported, not a failure", h.verdict("d0", 5000, 1024, {})[0])
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
