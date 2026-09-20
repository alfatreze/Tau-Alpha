#!/usr/bin/env python3
"""Decode Tau's fixed 64-byte Pocket SDRAM diagnostic result record.

The Pocket stores this as a binary file so the FPGA transfer is small and
bounded. This tool is the human/audit boundary: it emits stable JSON and
rejects bad size, magic, schema, or checksum rather than presenting a stale
or partial result as evidence.
"""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path


MAGIC = 0x544C4F47  # TLOG
WORDS = 16
BYTES = WORDS * 4


def decode(data: bytes) -> dict[str, object]:
    if len(data) != BYTES:
        raise ValueError(f"expected {BYTES} bytes, got {len(data)}")
    words = struct.unpack(">16I", data)
    if words[0] != MAGIC:
        raise ValueError(f"bad magic: 0x{words[0]:08X}")
    if words[1] != 0x00010040:
        raise ValueError(f"unsupported schema/length: 0x{words[1]:08X}")
    checksum = MAGIC
    for word in words[:12]:
        checksum ^= word
    if words[12] != checksum:
        raise ValueError(
            f"bad checksum: expected 0x{checksum:08X}, got 0x{words[12]:08X}"
        )
    flags = words[2]
    return {
        "format": "tau-diagnostic-log",
        "schema": 1,
        "stage": flags & 0xFF,
        "failed": bool(flags & 0x100),
        "timed_out": bool(flags & 0x200),
        "core_version": f"0x{words[3]:08X}",
        "run": words[4],
        "cycle_counter": words[5],
        "readback_checks": words[6],
        "failures": words[7],
        "first_fail_address": f"0x{words[8]:08X}",
        "expected": f"0x{words[9]:08X}",
        "actual": f"0x{words[10]:08X}",
        "status0": f"0x{words[11]:08X}",
        "checksum": f"0x{words[12]:08X}",
    }


def words_from_interact(doc: dict) -> bytes:
    """Rebuild the 64-byte record from APF's interact_persist.json (A-091).

    Variables 30..45 hold record words 0..14 as their low 31 bits and word 15
    as the withheld top bits (bit i = top bit of word i); APF stores signed
    int32, so a negative or missing value is rejected, not repaired.
    """
    by_id = {v["id"]: v["val"] for v in doc["interact_persist"]["variables"]}
    raw = []
    for i in range(16):
        if (30 + i) not in by_id:
            raise ValueError(f"interact variable {30 + i} missing")
        v = by_id[30 + i]
        if not 0 <= v <= 0x7FFFFFFF:
            raise ValueError(f"interact variable {30 + i} out of range: {v}")
        raw.append(v)
    mask = raw[15]
    words = [raw[i] | (((mask >> i) & 1) << 31) for i in range(15)] + [0]
    return struct.pack(">16I", *words)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path)
    parser.add_argument("--raw", action="store_true",
                        help="with --interact: print the 16 reconstructed words (A-092)")
    parser.add_argument("--interact", action="store_true",
                        help="path is APF's interact_persist.json (A-091)")
    args = parser.parse_args()
    if args.interact and args.raw:
        words = struct.unpack(">16I", words_from_interact(
            json.loads(args.path.read_text())))
        print(json.dumps({f"w{i}": f"0x{w:08X}" for i, w in enumerate(words)},
                         indent=2))
        return
    if args.interact:
        data = words_from_interact(json.loads(args.path.read_text()))
    else:
        data = args.path.read_bytes()
    print(json.dumps(decode(data), indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
