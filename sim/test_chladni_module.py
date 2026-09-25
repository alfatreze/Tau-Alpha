#!/usr/bin/env python3
"""Host test for fw/chladni.inc (B-276): the mailbox plane write, the SBLIT and the tile replication, checked pixel for pixel
against an independent rendering of the figure, with both possible mailbox half orders; the probe must also refuse a broken
mailbox; rate limit and audio-FIFO gating are checked. Emulated hardware, so it proves the logic, not the RTL."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
with tempfile.TemporaryDirectory() as td:
    exe = Path(td) / "h"
    r = subprocess.run(["cc", "-std=c11", "-O1", "-Wall", "-Wno-unused-function", "-Wno-unused-variable", "-o", str(exe),
                        str(ROOT / "sim/chladni_module_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    for name, args, want_rc, must in [
        ("mailbox halves in natural order", ["0", "1"], 0, ["wrong", "0 wrong"]),
        ("mailbox halves swapped", ["1", "1"], 0, ["swap=1", "0 wrong"]),
        ("a broken mailbox disables the meter", ["0", "0"], 0, ["ok=2", "toasts=1"]),
    ]:
        p = subprocess.run([str(exe)] + args, capture_output=True, text=True)
        good = p.returncode == want_rc and all(m in p.stdout for m in must)
        print(("ok   " if good else "FAIL ") + name)
        if not good: print(p.stdout); fails += 1
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
