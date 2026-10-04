#!/usr/bin/env python3
"""fw/tpg.h (C, the firmware's pixel source) must equal tools/tpg.py pixel for pixel, in both modes, at every size that matters."""
import os, subprocess, sys, tempfile
import numpy as np
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import tpg

fails = 0
rng = np.random.default_rng(11)
with tempfile.TemporaryDirectory() as d:
    exe = os.path.join(d, "h")
    subprocess.check_call(["cc", "-O2", "-I", os.path.join(ROOT, "fw"), os.path.join(ROOT, "sim/tpg_harness.c"), "-o", exe])
    for mode in (0, 1):
        for n in (0, 1, 5, 384, 400, 799, 2001, 5000, tpg.capacity(mode)):
            if mode == 0 and n > 20000 and n != tpg.capacity(0): continue
            rec = bytes(rng.integers(0, 256, n, dtype=np.uint8))
            p = os.path.join(d, "r.bin"); open(p, "wb").write(rec)
            out = subprocess.check_output([exe, p, str(mode)])
            rows = int.from_bytes(out[:4], "little")
            c_img = np.frombuffer(out[4:], "<u2").reshape(tpg.H, tpg.W)
            py = tpg.encode(rec, mode)
            ok = np.array_equal(c_img, py) and rows == tpg.rows_used(n, mode)
            print(("ok   " if ok else "FAIL ") + f"mode {'LR'[mode]} {n} B: C == Python, rows {rows}")
            fails += 0 if ok else 1
        out = subprocess.check_output([exe, p, str(mode)]) if False else None
    big = os.path.join(d, "big.bin"); open(big, "wb").write(bytes(tpg.capacity(1) + 1))
    r = subprocess.check_output([exe, big, "1"]).strip()
    ok = r == b"REFUSED"; print(("ok   " if ok else "FAIL ") + "mode R refuses one byte over"); fails += 0 if ok else 1
print("PASSED" if not fails else f"{fails} FAILED"); sys.exit(1 if fails else 0)
