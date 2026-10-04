#!/usr/bin/env python3
"""Checks that a PACKS=1 firmware and the packs the toolchain builds agree on the ABI addresses (docs/features/meters/METER_PACKS.md). Builds the 192 KB release with
PACKS=1 (about 1 minute) and verifies: the meter scratch is at MTR_PACK_SCRATCH_ORG and large enough, the heap ends where the scratch begins, and a Layered Wave pack built with
the tool's defaults is linked for exactly that scratch and for slot 0 of the PSRAM code window. Exit 1 on any disagreement. Not part of make test-host (it builds the firmware).

  python3 tools/check_packs_abi.py        (needs the vendored toolchain)"""
import os, re, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import pack_meter as pm


def const(name):
    m = re.search(r"#define\s+%s\s+(0x[0-9A-Fa-f]+)u?" % name, (ROOT / "fw/meter_pack.h").read_text())
    return int(m.group(1), 16)


def main():
    env = dict(os.environ, RAM_192K="1", CLK66="1", SDRAM_BUSY="1", LPC_FW="1", PACKS="1")
    r = subprocess.run(["bash", str(ROOT / "fw/build.sh"), "release"], capture_output=True, text=True, env=env, cwd=ROOT)
    if r.returncode:
        print(r.stdout[-1500:], r.stderr[-1500:]); return 1
    sy = pm.symbols(ROOT / "fw/fw.elf")
    scratch, size = const("MTR_PACK_SCRATCH_ORG"), const("MTR_PACK_SCRATCH_SIZE")
    bad = []
    if sy["__meter_scratch_start"][0] != scratch: bad.append("scratch is at 0x%X, the ABI says 0x%X" % (sy["__meter_scratch_start"][0], scratch))
    if sy["__meter_scratch_end"][0] - sy["__meter_scratch_start"][0] < size: bad.append("scratch is smaller than the ABI's %d B" % size)
    if sy["_heap_end"][0] != scratch: bad.append("the heap ends at 0x%X, not at the scratch 0x%X" % (sy["_heap_end"][0], scratch))
    blob, info = pm.build("layered_wave", const("MTR_PACK_SLOT_BASE"), ROOT / "work/layered_wave_abi.elf")
    import struct
    s_org = struct.unpack_from("<I", blob, 32)[0]
    if s_org != scratch: bad.append("a pack built with defaults is linked for scratch 0x%X, not 0x%X" % (s_org, scratch))
    if info["data"] + info["bss"] > size: bad.append("the Layered Wave pack's working state (%d B) does not fit the %d B scratch" % (info["data"] + info["bss"], size))
    for m in pm.METER_IDS:
        _, mi = pm.build(m, const("MTR_PACK_SLOT_BASE") + pm.SLOT[m] * 0x10000, ROOT / ("work/%s_abi.elf" % m))
        if mi["data"] + mi["bss"] > size: bad.append("the %s pack's working state (%d B) does not fit the %d B scratch" % (m, mi["data"] + mi["bss"], size))
    # PACKS_ONLY: the five meters must be absent from the firmware and the directory-built list present
    r = subprocess.run(["bash", str(ROOT / "fw/build.sh"), "release"], capture_output=True, text=True, env=dict(env, PACKS_ONLY="1"), cwd=ROOT)
    if r.returncode:
        print(r.stdout[-1500:], r.stderr[-1500:]); return 1
    sy2 = pm.symbols(ROOT / "fw/fw.elf")
    for name in ("wviz_bars_tick", "wviz_scope_tick", "chladni_tick_box", "vum_tick", "lw_tick", "chl_render"):
        if name in sy2: bad.append("a PACKS_ONLY firmware still contains %s" % name)
    for name in ("viz_list", "viz_n"):
        if name not in sy2: bad.append("a PACKS_ONLY firmware has no %s" % name)
    for b in bad: print("ABI MISMATCH: " + b)
    if not bad: print("packs ABI OK: scratch 0x%X (%d B used of %d), heap ends there, Layered Wave pack linked for it (%d B image)" % (scratch, info["data"] + info["bss"], size, info["load"]))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
