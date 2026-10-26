#!/usr/bin/env python3
"""A read-only view of tau-packs.bin (docs/features/TAU_PACKS_FORMAT.md): which meters a core's pack bundle carries, with every pack's header, its CRC check and the descriptor it ends with (name, parameters, presets,
the preview's palette and run-length stream). Nothing here writes the file. Tau Omega can port this walk (it is the whole format); the firmware's own reader is fw2/cores/meter_pack_core.h.

  python3 tools/tau_packs_info.py PATH/tau-packs.bin          JSON on stdout (exit 1 when the file is not a whole, sound bundle)
"""
import json, struct, sys, zlib
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent / "player2"))
import meter_desc

SLOT_BASE, SLOT_SIZE = 0x24900000, 0x10000


def parse(data: bytes):
    """-> {"version", "count", "bytes", "packs": [...]}; raises ValueError for a file that is not a whole bundle."""
    if len(data) < 8 or data[:4] != b"TPKB": raise ValueError("not a bundle (magic)")
    ver, count = struct.unpack_from("<HH", data, 4)
    if ver != 1: raise ValueError("bundle version %d (this reader knows 1)" % ver)
    off, packs = 8, []
    for i in range(count):
        if off + 48 > len(data) or data[off:off + 4] != b"TMPK": raise ValueError("pack %d: header missing or damaged" % i)
        abi, mid, size, org, entry, crc, d_off, d_size, s_org, bss, ps_off, ps_size = struct.unpack_from("<HHIIIIIIIIII", data, off + 4)
        if size == 0 or off + 48 + size > len(data): raise ValueError("pack %d runs past the end of the file" % i)
        image = data[off + 48:off + 48 + size]
        trailer = struct.unpack_from("<II", image, size - 8)
        desc = meter_desc.parse_desc(image[size - trailer[0]:]) if trailer[1] == meter_desc.TRAILER and trailer[0] <= size else None
        packs.append({"index": i, "meter_id": mid, "slot": (org - SLOT_BASE) // SLOT_SIZE, "abi_major": abi >> 8, "abi_minor": abi & 255, "image_bytes": size, "origin": "0x%08X" % org, "entry_offset": entry,
                      "crc32": "0x%08X" % crc, "crc_ok": zlib.crc32(image) & 0xFFFFFFFF == crc, "data_bytes": d_size, "bss_bytes": bss, "pstate_bytes": ps_size,
                      "name": desc["name"] if desc else None, "descriptor": ({k: (v.hex() if isinstance(v, (bytes, bytearray)) else v) for k, v in desc.items()} if desc else None)})
        off += 48 + size
    if off != len(data): raise ValueError("%d bytes after the last pack" % (len(data) - off))
    return {"version": ver, "count": count, "bytes": len(data), "packs": packs}


def main():
    if len(sys.argv) != 2: print(__doc__); return 2
    try: r = parse(Path(sys.argv[1]).read_bytes())
    except ValueError as e: print(json.dumps({"error": str(e)})); return 1
    print(json.dumps(r, indent=1)); return 0 if all(p["crc_ok"] for p in r["packs"]) else 1


if __name__ == "__main__":
    sys.exit(main())
