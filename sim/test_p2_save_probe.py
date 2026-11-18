#!/usr/bin/env python3
"""The save-slot self-test (fw2/cores/save_probe_core.h, S1 spike of docs/features/meters/METER_SAVE_FILE_SPEC.md) on the host: the firmware's own code against a RAM model (sim/p2_save_probe_harness.c),
chained like boots, with the file decoded by tools/decode_save_probe.py (the Mac side) after each one; a damaged body, a damaged header and a Quit in the middle of the self-test; the zlib CRC and the
pattern agree between C and Python; two mutants of the firmware core must FAIL (the CRC in the wrong byte order; the seed never advancing). The installer's zeroed file is checked in test_install_dev_core."""
import re, struct, subprocess, sys, tempfile, zlib
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import decode_save_probe as dec
import toolchain
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(extra)))
    if not ok: fails += 1
def build(td, bug=0):
    exe = Path(td) / f"svp{bug}"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", f"-DBUG={bug}", "-o", str(exe), str(ROOT / "sim/p2_save_probe_harness.c")], capture_output=True, text=True, env=toolchain.host_env())
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, inp, out, epoch, tear=None):
    cmd = [str(exe), inp, str(out), str(epoch)] + ([str(tear)] if tear is not None else [])
    o = subprocess.run(cmd, capture_output=True, text=True)
    assert o.returncode == 0, o.stderr
    return dict(kv.split("=") for kv in o.stdout.split())
with tempfile.TemporaryDirectory() as td:
    td = Path(td); exe = build(td)
    f = [td / f"f{i}.bin" for i in range(8)]
    r1 = run(exe, "-", f[0], 1700000000)
    check("first boot of a zero file: boots 1, nothing to verify", (r1["boots"], r1["first"], r1["crc"], r1["pat_bad"]) == ("1", "1", "0", "0"), r1)
    d = dec.decode(f[0].read_bytes())
    check("the Mac decoder agrees (status ok, boots 1, CRC stored = computed, pattern clean)", d["status"] == "ok" and d["boots"] == 1 and d["crc_stored"] == d["crc_computed"] and d["pattern_bad_words"] == 0, d)
    check("the firmware's CRC is zlib's (the same value the decoder computes from the bytes)", int(r1["crc_written"], 16) == zlib.crc32(f[0].read_bytes()[32:]) & 0xFFFFFFFF)
    r2 = run(exe, str(f[0]), f[1], 1700000100)
    r3 = run(exe, str(f[1]), f[2], 1700000200)
    check("second boot: boots 2, CRC ok, pattern ok, the value the first boot wrote comes back, its epoch too", (r2["boots"], r2["first"], r2["crc"], r2["pat_bad"], r2["last_loaded"], r2["prev_epoch"]) == ("2", "0", "1", "0", r1["last_written"], "1700000000"), (r1, r2))
    check("third boot: boots 3 and the chain continues", (r3["boots"], r3["crc"], r3["last_loaded"]) == ("3", "1", r2["last_written"]), r3)
    check("each boot writes a NEW pattern (the seed advances and the file changes)", f[0].read_bytes() != f[1].read_bytes() and r1["seed"] == "1" and r2["seed"] == "2" and r3["seed"] == "3")
    # damaged body: one flipped bit in the middle
    b = bytearray(f[2].read_bytes()); b[4000] ^= 0x10; f[3].write_bytes(b)
    r = run(exe, str(f[3]), f[4], 1)
    check("one flipped bit in the body: CRC bad and exactly one pattern word wrong", (r["crc"], r["pat_bad"]) == ("2", "1"), r)
    check("the Mac decoder names the same damage", dec.decode(bytes(b))["status"] == "bad" and dec.decode(bytes(b))["pattern_bad_words"] == 1)
    # damaged header: the magic gone -> treated as a first boot, never a refusal
    b = bytearray(f[2].read_bytes()); b[0] ^= 0xFF; f[5].write_bytes(b)
    r = run(exe, str(f[5]), f[6], 1)
    check("a damaged magic starts afresh (first boot, boots 1), the self-test never refuses to run", (r["first"], r["boots"]) == ("1", "1"), r)
    # a Quit in the middle of the self-test: 500 writes land, the rest are lost
    r = run(exe, str(f[2]), f[7], 1700000300, tear=500)
    r_after = run(exe, str(f[7]), td / "after.bin", 1700000400)
    check("a torn update is seen at the next boot as a CRC mismatch (old header, half new body)", r_after["crc"] == "2" and int(r_after["pat_bad"]) > 0, r_after)
    # all-zero and all-ones (bit 5 of the slot's parameters: the slot is filled with 0xFF when no file exists)
    (td / "ff.bin").write_bytes(b"\xff" * 8192)
    r = run(exe, str(td / "ff.bin"), td / "ff2.bin", 1)
    check("an all-0xFF file (the initialisation by the Pocket) is a first boot", (r["first"], r["boots"]) == ("1", "1"), r)
    check("the decoder: a zero file is reported as 'nothing came back'", dec.decode(bytes(8192))["status"] == "zero" and dec.decode(b"\0" * 100)["status"] == "bad")
    # mutants
    for bug, what in ((1, "the seed never advances"), (2, "the CRC in big-endian byte order")):
        m = build(td, bug)
        x1 = run(m, "-", td / "m1.bin", 5); x2 = run(m, str(td / "m1.bin"), td / "m2.bin", 6)
        d1 = dec.decode((td / "m1.bin").read_bytes())
        killed = ((bug == 1 and x2["seed"] == x1["seed"]) or (bug == 2 and d1["status"] != "ok"))
        check(f"mutant {bug} ({what}) is caught", killed, (x1, x2))
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
