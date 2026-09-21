#!/usr/bin/env python3
"""Host test for the PSRAM record decoder: accepts a good record, and rejects or
flags bad magic, bad checksum, a wrong per-die CRC, a missing guard hit, sticky
faults and a firmware timeout. The record is built here from the same fill/CRC
definition the firmware uses (fw/psram_diag.c)."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
from decode_tau_diag_log import (PSRAM_MAGIC, PSRAM_WIN_MAGIC, decode_psram,  # noqa: E402
                                 decode_psram_window, psram_expected_chain, psram_expected_crc)

LOG2 = 6


def record(**over):
    w = [0] * 16
    w[0] = PSRAM_MAGIC
    w[1] = 1 | (LOG2 << 24)
    w[4] = 0x09 << 16
    w[2] = 384
    w[3] = 0
    w[4] |= 0x20 | 0x80         # guard hit + WAIT_HI seen (T_ACC 9 already in [23:16])
    for d in range(4):
        w[5 + d] = psram_expected_crc(d, LOG2)
    w[13] = 760
    for k, v in over.items():
        w[int(k[1:])] = v
    s = PSRAM_MAGIC
    for x in w[:14]:
        s ^= x
    w[14] = s
    return w


def fix_sum(w):
    s = PSRAM_MAGIC
    for x in w[:14]:
        s ^= x
    w[14] = s
    return w


fails = 0


def check(name, cond):
    global fails
    print(("ok:   " if cond else "FAIL: ") + name)
    fails += not cond


check("good record passes", decode_psram(record())["verdict"] == "PASS")
r = decode_psram(record(w1=(1 | (2 << 17) | (LOG2 << 24))))
check("sample index = T_ACC + read extra", r["t_acc"] == 9 and r["read_extra_clocks"] == 2 and r["sample_index"] == 11)
check("plain run has extra 0", decode_psram(record())["sample_index"] == 9 and not decode_psram(record())["slow_dials"])
try:
    decode_psram(record(w0=0x12345678)); check("bad magic rejected", False)
except ValueError:
    check("bad magic rejected", True)
w = record(); w[14] ^= 1
try:
    decode_psram(w); check("bad checksum rejected", False)
except ValueError:
    check("bad checksum rejected", True)
w = record(); w[7] ^= 0x100; fix_sum(w)
r = decode_psram(w)
check("wrong die CRC fails", r["verdict"] == "FAIL" and not r["dies"][2]["crc_match"])
check("missing guard hit fails", decode_psram(record(w4=0x090080))["verdict"] == "FAIL")
check("CE conflict fails", decode_psram(record(w4=0x090030))["verdict"] == "FAIL")
check("controller timeout fails", decode_psram(record(w4=0x090024))["verdict"] == "FAIL")
check("firmware timeout fails", decode_psram(record(w4=0x090120))["verdict"] == "FAIL")
r = decode_psram(record(w3=1, w9=0x00010000, w10=(3 << 24) | 0x40005, w11=0x11, w12=0x10))
check("failure reported with die and first fault",
      r["verdict"] == "FAIL" and r["dies"][2]["failures"] == 1
      and r["first_failure"]["test"] == 3 and r["first_failure"]["word"] == "0x040005")
# ---- CPU-window record (PSW1) ----
def wrecord(**over):
    w = [0] * 16
    w[0] = PSRAM_WIN_MAGIC
    w[1] = 1 | (4 << 17) | (LOG2 << 24)
    w[2] = 480
    w[4] = (0x09 << 16) | (1 << 24) | (1 << 25) | 0x20 | 0x80
    w[5] = psram_expected_chain(LOG2)
    w[6] = (300 << 16) | 34
    w[7] = (400 << 16) | 33
    w[13] = 990
    for k, v in over.items():
        w[int(k[1:])] = v
    s = PSRAM_WIN_MAGIC
    for x in w[:14]:
        s ^= x
    w[14] = s
    return w


r = decode_psram_window(wrecord())
check("window record passes", r["verdict"] == "PASS" and r["cost_cycles"]["read_avg"] == 34 and r["cost_cycles"]["write_max"] == 400)
check("window: wrong CRC chain fails", decode_psram_window(wrecord(w5=1))["verdict"] == "FAIL")
check("window: cross-check failure fails", decode_psram_window(wrecord(w8=2))["verdict"] == "FAIL")
check("window: guard not ok fails", decode_psram_window(wrecord(w4=(0x09 << 16) | (1 << 25) | 0xA0))["verdict"] == "FAIL")
check("window: missing guard hit fails", decode_psram_window(wrecord(w4=(0x09 << 16) | (3 << 24) | 0x80))["verdict"] == "FAIL")
check("window: soak mode reported", decode_psram_window(wrecord(w1=3 | (5 << 17) | (LOG2 << 24)))["mode"] == "soak")
w = wrecord(); w[14] ^= 1
try:
    decode_psram_window(w); check("window: bad checksum rejected", False)
except ValueError:
    check("window: bad checksum rejected", True)
try:
    decode_psram_window(record()); check("window: mailbox record rejected", False)
except ValueError:
    check("window: mailbox record rejected", True)
print("PASSED (0 failures)" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
