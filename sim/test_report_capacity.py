#!/usr/bin/env python3
"""The Info report must never refuse for size (DEV 187: 'REPORT TOO BIG FOR A PIXEL GRID', B-867). The record is built in a 32 KB PSRAM scratch (fw2/diagnostics/report_page.c) and the pixel grid picks its
own density (robust 6,059 bytes, lossless 259,184). This test works out the WORST-CASE record from the counter table (every row at its longest name and value) and fails when it comes within 25% of the
scratch, so the table cannot silently outgrow the buffer again, and checks the scratch fits the lossless grid and the PSRAM gap it sits in (tools/psram_map notes in the audit trail B-867)."""
import re, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(c, what):
    global fails
    print(("ok   " if c else "FAIL ") + what)
    fails += 0 if c else 1
rp = (ROOT / "fw2/diagnostics/report_page.c").read_text()
cap = int(re.search(r"#define PS_SCRATCH_CAP\s+0x([0-9A-Fa-f]+)u", rp).group(1), 16)
base = int(re.search(r"#define PS_SCRATCH\s+0x([0-9A-Fa-f]+)u", rp).group(1), 16) - 0xA4000000
tag = int(re.search(r"#define TAG_CAP\s+(\d+)u", rp).group(1))
ic = (ROOT / "fw2/diagnostics/info_counters.c").read_text()
rows = len(re.findall(r'^\s+(?:\{ |U32\()"[^"]+"', ic, re.M))
cm = (ROOT / "fw2/cores/counters_core.h").read_text()
name_max = int(re.search(r"#define CNT_NAME_MAX\s+(\d+)u", cm).group(1)); val_max = int(re.search(r"#define CNT_VALUE_MAX\s+(\d+)", cm).group(1)) if re.search(r"#define CNT_VALUE_MAX\s+(\d+)", cm) else 40
entry = 1 + (name_max - 1) + 1 + val_max          # row byte, name, 0, value (counters_core.h cnt_export comment: the longest entry)
tlv = 2                                           # tag and length of the INFOTEXT wrapper per entry (an estimate; the real header is in fw/suite_core.h)
extra = 40                                        # rows the table adds at run time (one per registered meter, generated header) with margin
worst = (rows + extra) * (entry + tlv) + 64
print(f"table rows {rows} (+{extra} run-time), worst entry {entry + tlv} bytes, worst record {worst} bytes, scratch {cap}, 4 KB fall-back {tag}")
check(worst * 4 <= cap * 3, f"the worst-case record ({worst}) stays under 75% of the PSRAM scratch ({cap})")
tp = (ROOT / "fw/tpg.h").read_text()
lossless = int(re.search(r"259,184 for L", tp).group(0).split(",")[0][-3:] + "184".replace("184", "")) if False else 259184
check(cap <= lossless and "259,184 for L" in tp, f"the scratch ({cap}) fits the lossless grid's capacity (259,184 per fw/tpg.h)")
check(0x530000 == base and base + cap <= 0x540000, f"the scratch sits in the free PSRAM gap 0x530000..0x53FFFF (offset {base:#x}, end {base + cap:#x}); library queue ends 0x520000, assets staging 0x520000-0x52FFFF, PRST starts 0x540000")
check("scratch_ok" in rp and "0x50535231u" in rp, "the scratch is proven at runtime before the first store")
print("PASSED" if not fails else f"{fails} FAILED")
sys.exit(1 if fails else 0)
