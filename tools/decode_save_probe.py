#!/usr/bin/env python3
"""Read the meter-settings save slot's PROBE file (tau-save-probe.bin, S1 spike of docs/features/meters/METER_SAVE_FILE_SPEC.md) back on the Mac after a Quit and say what is in it.

  python3 tools/decode_save_probe.py /Volumes/Pock/Saves/tau/<core folder>/tau-save-probe.bin       (add --json for a machine-readable line)
  python3 tools/decode_save_probe.py --make FILE                                                  (write the zeroed 8,192-byte file the installer creates)

Layout (fw2/cores/save_probe_core.h): 2,048 little-endian words: magic 'TSAV', version, boots, last value, seed, CRC-32 of the body (bytes 32..8191), epoch, 0, then the pattern. The pattern and the
CRC are the same functions as the firmware's (compared in sim/test_p2_save_probe.py). Exit code 0 = a sound probe file with boots >= 1, 1 = a zero file (nothing came back), 2 = damaged."""
import json, struct, sys, zlib
WORDS, HDR, MAGIC, VERSION = 2048, 8, 0x56415354, 1
M = 0xFFFFFFFF


def pat(seed, i):
    x = (seed * 0x9E3779B1 + i * 0x85EBCA6B) & M
    x ^= x >> 15; x = (x * 0x2C1B3C6D) & M; x ^= x >> 12; x = (x * 0x297A2D39) & M; x ^= x >> 15
    return x


def decode(data):
    if len(data) != WORDS * 4:
        return {"status": "bad", "why": f"size {len(data)} instead of {WORDS * 4}"}
    w = struct.unpack("<%dI" % WORDS, data)
    if not any(w):
        return {"status": "zero", "why": "all zero: the file was never written back (or the core never wrote it)"}
    if w[0] != MAGIC or w[1] != VERSION:
        return {"status": "bad", "why": f"magic {w[0]:08x} version {w[1]}"}
    crc = zlib.crc32(data[HDR * 4:]) & M
    bad = sum(1 for i in range(HDR, WORDS) if w[i] != pat(w[4], i))
    return {"status": "ok" if crc == w[5] and bad == 0 else "bad", "boots": w[2], "last_value": f"{w[3]:08x}", "seed": w[4], "crc_stored": f"{w[5]:08x}", "crc_computed": f"{crc:08x}",
            "pattern_bad_words": bad, "epoch": w[6], "why": "" if crc == w[5] and bad == 0 else "the body does not match its CRC or its pattern"}


def main(argv):
    if len(argv) == 3 and argv[1] == "--make":
        open(argv[2], "wb").write(bytes(WORDS * 4)); print(f"wrote {argv[2]}: {WORDS * 4} zero bytes"); return 0
    args = [a for a in argv[1:] if not a.startswith("--")]
    if len(args) != 1:
        print(__doc__); return 2
    r = decode(open(args[0], "rb").read())
    if "--json" in argv:
        print(json.dumps(r))
    else:
        for k, v in r.items(): print(f"{k:18} {v}")
    return {"ok": 0, "zero": 1}.get(r["status"], 2)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
