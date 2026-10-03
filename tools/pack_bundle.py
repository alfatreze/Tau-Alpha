#!/usr/bin/env python3
"""Bundle meter packs into tau-packs.bin, the file the firmware reads from its data slot (fw/meter_pack_core.h: 'TPKB', version 1, count, then the packs back to back).

  python3 tools/pack_bundle.py OUT.bin PACK.tmpk [PACK.tmpk ...]

Each pack must have been built for the firmware's real slot origin (tools/pack_meter.py --org); the bundle only concatenates and checks that every file is a whole pack."""
import struct, sys
from pathlib import Path


def bundle(blobs):
    for i, b in enumerate(blobs):
        if len(b) < 48 or b[:4] != b"TMPK" or len(b) != 48 + struct.unpack_from("<I", b, 8)[0]:
            raise SystemExit("pack %d is not a whole .tmpk file" % i)
    return b"TPKB" + struct.pack("<HH", 1, len(blobs)) + b"".join(blobs)


def main():
    if len(sys.argv) < 3:
        print(__doc__); return 2
    out = Path(sys.argv[1]); out.parent.mkdir(parents=True, exist_ok=True)
    data = bundle([Path(p).read_bytes() for p in sys.argv[2:]])
    out.write_bytes(data)
    print("%s: %d B, %d pack(s)" % (out, len(data), len(sys.argv) - 2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
