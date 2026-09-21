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

PSRAM_MAGIC = 0x50535231  # "PSR1"


def _psram_hash(a: int) -> int:
    """Deterministic fill word; must match hash_word() in fw/psram_diag.c."""
    m = 0xFFFFFFFF
    x = (a * 0x9E3779B1 + 0x7F4A7C15) & m
    x ^= x >> 15
    x = (x * 0x85EBCA6B) & m
    x ^= x >> 13
    return x


def _psram_crc_step(crc: int, w: int) -> int:
    crc ^= w
    return (((crc << 5) | (crc >> 27)) & 0xFFFFFFFF) ^ 0x9E3779B9


def psram_expected_crc(die: int, fill_log2: int) -> int:
    """CRC the firmware must read back for a die that returned exactly the fill."""
    crc, base = 0, die << 21
    for i in range(1 << fill_log2):
        crc = _psram_crc_step(crc, _psram_hash(base + i))
    return crc


PSRAM_WIN_MAGIC = 0x50535731  # "PSW1"


def psram_expected_chain(fill_log2: int) -> int:
    """CRC of all four dies' fills read back in order, one running CRC (window record)."""
    crc = 0
    for d in range(4):
        base = d << 21
        for i in range(1 << fill_log2):
            crc = _psram_crc_step(crc, _psram_hash(base + i))
    return crc


def decode_psram_window(words) -> dict[str, object]:
    """Decode the B-016 CPU-window record (PSW1, words 0..14).

    Window suite: the same tests as the mailbox suite but through CPU loads and stores
    at 0xA4000000, a mailbox-versus-window cross-check, and the per-access cycle cost.
    The expected CRC chain is recomputed from the deterministic fill here.
    """
    if words[0] != PSRAM_WIN_MAGIC:
        raise ValueError(f"not a PSRAM window record: 0x{words[0]:08X}")
    checksum = PSRAM_WIN_MAGIC
    for w in words[:14]:
        checksum ^= w
    if words[14] != checksum:
        raise ValueError(f"bad checksum: expected 0x{checksum:08X}, got 0x{words[14]:08X}")
    fill_log2 = (words[1] >> 24) & 0x1F
    status = words[4] & 0xFF
    want = psram_expected_chain(fill_log2)
    guard_ok = bool((words[4] >> 24) & 1)
    cross_ok = bool((words[4] >> 25) & 1)
    ok = (words[3] == 0 and words[8] == 0 and not ((words[4] >> 8) & 1) and guard_ok and cross_ok
          and words[5] == want and not status & 0x14 and bool(status & 0x20))
    mode = (words[1] >> 17) & 7
    return {
        "format": "tau-psram-cpu-window", "verdict": "PASS" if ok else "FAIL",
        "mode": "soak" if mode == 5 else "window",
        "passes": words[1] & 0xFFFF, "fill_words_per_die": 1 << fill_log2,
        "checks": words[2], "failures": words[3], "cross_check_failures": words[8],
        "t_acc": (words[4] >> 16) & 0xFF, "window_ops": words[13],
        "crc_chain_read": f"0x{words[5]:08X}", "crc_chain_expected": f"0x{want:08X}",
        "crc_chain_match": words[5] == want,
        "cost_cycles": {"read_avg": words[6] & 0xFFFF, "read_max": words[6] >> 16,
                        "write_avg": words[7] & 0xFFFF, "write_max": words[7] >> 16},
        "guard_ok": guard_ok, "cross_check_ok": cross_ok,
        "flags": {"timeout": bool(status & 0x04), "ce_conflict": bool(status & 0x10),
                  "guard_hit": bool(status & 0x20), "wait_lo_seen": bool(status & 0x40),
                  "wait_hi_seen": bool(status & 0x80),
                  "firmware_timeout": bool((words[4] >> 8) & 1)},
        "die_failures_last_pass": [(words[9] >> (8 * d)) & 0xFF for d in range(4)],
        "first_failure": None if words[3] == 0 else {
            "test": words[10] >> 24, "word": f"0x{words[10] & 0xFFFFFF:06X}",
            "expected": f"0x{words[11]:08X}", "actual": f"0x{words[12]:08X}"},
    }


def decode_psram(words) -> dict[str, object]:
    """Decode the B-004 PSRAM diagnostic record (words 0..14 of the interact record).

    Rejects a bad magic or checksum; recomputes each die's expected CRC from the
    deterministic fill instead of trusting the firmware's own verdict.
    """
    if words[0] != PSRAM_MAGIC:
        raise ValueError(f"not a PSRAM record: 0x{words[0]:08X}")
    checksum = PSRAM_MAGIC
    for w in words[:14]:
        checksum ^= w
    if words[14] != checksum:
        raise ValueError(f"bad checksum: expected 0x{checksum:08X}, got 0x{words[14]:08X}")
    fill_log2 = (words[1] >> 24) & 0x1F
    status = words[4] & 0xFF
    dies = []
    for d in range(4):
        want = psram_expected_crc(d, fill_log2)
        dies.append({
            "die": d, "chip": d >> 1, "ce": d & 1,
            "failures": (words[9] >> (8 * d)) & 0xFF,
            "crc_read": f"0x{words[5 + d]:08X}", "crc_expected": f"0x{want:08X}",
            "crc_match": words[5 + d] == want,
        })
    ok = (words[3] == 0 and not ((words[4] >> 8) & 1) and all(x["crc_match"] for x in dies)
          and not status & 0x14 and bool(status & 0x20))
    return {
        "format": "tau-psram-diagnostic", "verdict": "PASS" if ok else "FAIL",
        "run": words[1] & 0xFFFF, "slow_dials": bool((words[1] >> 16) & 1),
        # margin experiment (B-012): the build's read-sample index, the extra read
        # clocks used by this run and the effective index (T_ACC + extra)
        "t_acc": (words[4] >> 16) & 0xFF, "read_extra_clocks": (words[1] >> 17) & 7,
        "sample_index": ((words[4] >> 16) & 0xFF) + ((words[1] >> 17) & 7),
        "fill_words_per_die": 1 << fill_log2,
        "checks": words[2], "failures": words[3], "controller_ops": words[13],
        "flags": {"timeout": bool(status & 0x04), "ce_conflict": bool(status & 0x10),
                  "guard_hit": bool(status & 0x20), "wait_lo_seen": bool(status & 0x40),
                  "wait_hi_seen": bool(status & 0x80),
                  "firmware_timeout": bool((words[4] >> 8) & 1)},
        "dies": dies,
        "first_failure": None if words[3] == 0 else {
            "test": words[10] >> 24, "word": f"0x{words[10] & 0xFFFFFF:06X}",
            "expected": f"0x{words[11]:08X}", "actual": f"0x{words[12]:08X}"},
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path)
    parser.add_argument("--full", action="store_true",
                        help="with --interact: decode an A-100 coverage record")
    parser.add_argument("--soak", action="store_true",
                        help="with --interact: decode an A-097 soak record")
    parser.add_argument("--psram-window", action="store_true",
                        help="with --interact: decode a B-016 PSRAM CPU-window record (PSW1)")
    parser.add_argument("--psram", action="store_true",
                        help="with --interact: decode a B-004 PSRAM diagnostic record")
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
    if args.interact and args.psram_window:
        words = struct.unpack(">16I", words_from_interact(
            json.loads(args.path.read_text())))
        print(json.dumps(decode_psram_window(words), indent=2, sort_keys=True))
        return
    if args.interact and args.psram:
        words = struct.unpack(">16I", words_from_interact(
            json.loads(args.path.read_text())))
        print(json.dumps(decode_psram(words), indent=2, sort_keys=True))
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
