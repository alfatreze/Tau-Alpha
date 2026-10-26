#!/usr/bin/env python3
"""The pack addresses the firmware and a pack are both built for must agree (fw2/cores/meter_pack_core.h is the one source; fw2/platform/link.ld derives the scratch from the stack/ring/tag layout):
    python3 tools/player2/check_packs_abi.py work/heapcheck/packs/player2/fw2.elf
Checks, on a PACKS=1 firmware ELF: _pack_scratch equals MTR_PACK_SCRATCH_ORG, the 4 KB scratch ends exactly at the tag zone, the image and its minimum free heap stay below the scratch, and every pack slot lies
above the cold image's 1 MiB PSRAM region and inside the 16 MiB PSRAM."""
import re, subprocess, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import pack_meter as pm
def main():
    elf = sys.argv[1]
    nm = subprocess.run([pm.tool("nm"), elf], capture_output=True, text=True, check=True).stdout
    sy = {ln.split()[-1]: int(ln.split()[0], 16) for ln in nm.splitlines() if len(ln.split()) == 3}
    bad = []
    if sy["_pack_scratch"] != pm.SCRATCH_ORG: bad.append("_pack_scratch 0x%X != MTR_PACK_SCRATCH_ORG 0x%X" % (sy["_pack_scratch"], pm.SCRATCH_ORG))
    if sy["_pack_scratch"] + 0x1000 != sy["_tag_start"]: bad.append("the scratch does not end at the tag zone")
    if sy["_image_end"] + 1024 > sy["_pack_scratch"]: bad.append("the image leaves less than the minimum free heap below the scratch")
    cold_end = 0x24800000 + 0x100000
    n = int(re.search(r"#define MTR_PACK_SLOTS\s+(\d+)u", pm._CORE).group(1))
    if pm.SLOT_BASE < cold_end: bad.append("the pack slots overlap the cold image region")
    if pm.SLOT_BASE + n * pm.SLOT_SIZE > 0x24000000 + 0x1000000: bad.append("the pack slots run past the 16 MiB PSRAM")
    print("pack ABI agrees with the firmware (scratch 0x%X, slots 0x%X..0x%X)" % (pm.SCRATCH_ORG, pm.SLOT_BASE, pm.SLOT_BASE + n * pm.SLOT_SIZE) if not bad else "FAIL: " + "; ".join(bad))
    return 1 if bad else 0
sys.exit(main())
