#!/usr/bin/env python3
"""Build tau-packs.bin: every meter pack of Player 2 in slot order, bundled ('TPKB', fw2/cores/meter_pack_core.h), the file a PACKS=1 core ships next to tau.rom and tau-cold.bin (data slot 9, the core's own folder).

  python3 tools/player2/build_packs.py OUT.bin [--hang KEY[:MODE]] [--window-probe]

--hang swaps one meter's pack for its hang test variant (fw2/ui/meter_pack_<KEY>_hang.c, the pack watchdog's Pocket test, docs/features/PACK_WATCHDOG_PLAN.md W4; MODE 0 = plain endless loop, 1 = endless
loop issuing engine commands; default 0). Only a test package asks for it; no release does.

--window-probe appends, after the normal packs, a second copy of every pack linked for the free slot WINDOW_SLOT (7) with meter id PROBE_ID_BASE + id (0x100 + id): the instrument of the external-meters spike
(docs/features/meters/EXTERNAL_METERS_PLAN.md, phase P0; fw2/diagnostics/pack_swap_probe.c). The normal installer skips ids it has no slot for, so a probe bundle behaves exactly like the normal one for the player.

Which meters are packs, their slots and ids come from the manifests (tools/player2/pack_meter.py): every meter with a p2 block. The packs are built from the CURRENT source, so call this from the same
tree and commit as the firmware it ships with (tools/package_dev_build.py does, for a PACKS=1 build): a pack is linked against the host table of the firmware's ABI and refused (E22) by any other.
"""
import sys, tempfile
WINDOW_SLOT = 7            # the free pack slot (MTR_PACK_SLOTS is 8, the registry uses 0..6): the one execution window of the lazy-loading design
PROBE_ID_BASE = 0x100      # fw2/cores/meter_pack_core.h MTR_PACK_PROBE_ID_BASE
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import pack_meter as pm, pack_bundle as pb


def build_window(workdir: Path):
    """[(key, blob)] the probe copies: each pack linked for the window slot, its header id PROBE_ID_BASE + id"""
    out = []
    for key in sorted(pm.SLOT, key=lambda k: pm.SLOT[k]):
        blob, _ = pm.build(key, pm.SLOT_BASE + WINDOW_SLOT * pm.SLOT_SIZE, workdir / (key + "_win.elf"), header_id=PROBE_ID_BASE + pm.METER_IDS[key])
        out.append((key + "@window", blob))
    return out


def build_all(workdir: Path, hang=None):
    """[(key, blob)] in slot order; hang = (key, mode) builds that meter's hang test pack instead"""
    out = []
    for key in sorted(pm.SLOT, key=lambda k: pm.SLOT[k]):
        if hang and key == hang[0]:
            blob, _ = pm.build(key, pm.SLOT_BASE + pm.SLOT[key] * pm.SLOT_SIZE, workdir / (key + ".elf"), ["-DHANG_MODE=%d" % hang[1]], source=key + "_hang")
        else:
            blob, _ = pm.build(key, pm.SLOT_BASE + pm.SLOT[key] * pm.SLOT_SIZE, workdir / (key + ".elf"))
        out.append((key, blob))
    return out


def main():
    argv = sys.argv[1:]
    hang = None
    window = "--window-probe" in argv
    argv = [a for a in argv if a != "--window-probe"]
    if len(argv) == 3 and argv[1] == "--hang":
        k, _, m = argv[2].partition(":"); hang = (k, int(m or 0)); argv = argv[:1]
        if k not in pm.SLOT: print("no such meter pack: " + k); return 2
    if len(argv) != 1:
        print(__doc__); return 2
    out = Path(argv[0]); out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as td:
        packs = build_all(Path(td), hang)
        if window:
            packs += build_window(Path(td))
    out.write_bytes(pb.bundle([b for _, b in packs]))
    print("%s: %d B, %d pack(s): %s" % (out, out.stat().st_size, len(packs), ", ".join(k for k, _ in packs)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
