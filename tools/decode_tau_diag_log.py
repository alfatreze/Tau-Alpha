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


def decode_soak(words) -> dict[str, object]:
    """Decode the A-097 soak record (words 0..14 of the interact record)."""
    if words[0] != 0x534F4B31:
        raise ValueError(f"not a soak record: 0x{words[0]:08X}")
    ms = lambda c: round(c / 60000.0, 3)          # 60 MHz core clock -> ms
    return {
        "format": "tau-cpu-window-soak",
        "passes": words[1], "checks": words[2], "failures": words[3],
        "matrix_failures": words[11], "random_failures": words[12],
        "matrix_timeouts": words[13],
        "first_failing_pass": words[4],
        "first_fail_address": f"0x{words[5]:08X}",
        "first_fail_expected": f"0x{words[6]:08X}",
        "first_fail_actual": f"0x{words[7]:08X}",
        "elapsed_seconds": words[8],
        "elapsed": f"{words[8] // 3600}:{(words[8] // 60) % 60:02d}:{words[8] % 60:02d}",
        "pass_ms_min": ms(words[9]), "pass_ms_max": ms(words[10]),
    }


def decode_full(words) -> dict[str, object]:
    """Decode the A-100 full-range coverage record."""
    if words[0] != 0x46554C31:
        raise ValueError(f"not a coverage record: 0x{words[0]:08X}")
    fb = words[8]
    return {
        "format": "tau-cpu-window-coverage",
        "address_line_checks": words[1], "address_line_failures": words[2],
        "first_address_fail": f"0x{words[3]:08X}",
        "first_address_expected": f"0x{words[4]:08X}",
        "first_address_actual": f"0x{words[5]:08X}",
        "crc_rounds": words[6], "crc_block_mismatches": words[7],
        "first_bad_block": None if fb == 0xFFFF else {"round": fb >> 8, "block": fb & 0xFF},
        "block0_crc_written": f"0x{words[9]:08X}", "block0_crc_read": f"0x{words[10]:08X}",
        "max_read_cycles": words[11], "max_write_cycles": words[12],
        "draw_engine_stall_cycles": words[13],
        "cpu_window_accesses_thousands": words[14],
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path)
    parser.add_argument("--full", action="store_true",
                        help="with --interact: decode an A-100 coverage record")
    parser.add_argument("--soak", action="store_true",
                        help="with --interact: decode an A-097 soak record")
    parser.add_argument("--raw", action="store_true",
                        help="with --interact: print the 16 reconstructed words (A-092)")
    parser.add_argument("--interact", action="store_true",
                        help="path is APF's interact_persist.json (A-091)")
    args = parser.parse_args()
    if args.interact and args.full:
        words = struct.unpack(">16I", words_from_interact(
            json.loads(args.path.read_text())))
        print(json.dumps(decode_full(words), indent=2, sort_keys=True))
        return
    if args.interact and args.soak:
        words = struct.unpack(">16I", words_from_interact(
            json.loads(args.path.read_text())))
        print(json.dumps(decode_soak(words), indent=2, sort_keys=True))
        return
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
