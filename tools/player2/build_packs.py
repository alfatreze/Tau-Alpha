#!/usr/bin/env python3
"""Build tau-packs.bin: every meter pack of Player 2 in slot order, bundled ('TPKB', fw2/cores/meter_pack_core.h), the file a PACKS=1 core ships next to tau.rom and tau-cold.bin (data slot 9, the core's own folder).

  python3 tools/player2/build_packs.py OUT.bin

Which meters are packs, their slots and ids come from the manifests (tools/player2/pack_meter.py): every meter with a p2 block. The packs are built from the CURRENT source, so call this from the same
tree and commit as the firmware it ships with (tools/package_dev_build.py does, for a PACKS=1 build): a pack is linked against the host table of the firmware's ABI and refused (E22) by any other.
"""
import sys, tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import pack_meter as pm, pack_bundle as pb


def build_all(workdir: Path):
    """[(key, blob)] in slot order"""
    out = []
    for key in sorted(pm.SLOT, key=lambda k: pm.SLOT[k]):
        blob, _ = pm.build(key, pm.SLOT_BASE + pm.SLOT[key] * pm.SLOT_SIZE, workdir / (key + ".elf"))
        out.append((key, blob))
    return out


def main():
    if len(sys.argv) != 2:
        print(__doc__); return 2
    out = Path(sys.argv[1]); out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as td:
        packs = build_all(Path(td))
    out.write_bytes(pb.bundle([b for _, b in packs]))
    print("%s: %d B, %d pack(s): %s" % (out, out.stat().st_size, len(packs), ", ".join(k for k, _ in packs)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
