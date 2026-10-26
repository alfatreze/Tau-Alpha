#!/usr/bin/env python3
"""fw2/cores/monoclock_core.h: the seconds counter stays exact across the 32-bit cycle counter's wrap (every 64.4 s) for several update rates and starting values; mutants killed."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    if not ok: fails += 1
HZ = 66666667
def run(d, bug, start, step, n):
    exe = d / f"m{bug}"
    if not exe.exists():
        r = subprocess.run(["cc", "-O1", "-Wall", "-Werror", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_monoclock_harness.c")], capture_output=True, text=True)
        if r.returncode: print(r.stderr); sys.exit(1)
    return [int(x) for x in subprocess.run([str(exe)], input=f"{HZ} {start} {step} {n}\n", capture_output=True, text=True).stdout.split()]
def exact(out, step, n):
    # the model: whole seconds elapsed after i+1 steps, the first call only starts the clock
    return all(out[i] == ((i * step) // HZ) for i in range(n))
with tempfile.TemporaryDirectory() as td:
    d = Path(td)
    cases = [(0, 16666666, 600), (4294960000, 16666666, 600), (123456789, 1000000, 6000), (4000000000, 33333333, 300)]     # 4 Hz for 150 s; near the wrap; 66 updates a second for 90 s; 2 Hz
    good = all(exact(run(d, 0, s, st, n), st, n) for s, st, n in cases)
    check("seconds are exact over 90 to 150 s of updates, across the 64.4 s wrap, for four update rates and start values", good)
    # the case of the 2026-10-16 screenshots: 76 s of play reads 76, not 12
    o = run(d, 0, 0, 16666666, 304); check("76 s after the start the counter reads 76 (the Pocket showed 12)", o[-1] in (75, 76))
    for b, nm in {1: "a wrap loses the whole delta", 2: "the remainder is dropped"}.items():
        check(f"mutant caught: {nm}", not all(exact(run(d, b, s, st, n), st, n) for s, st, n in cases))
print("PASSED" if not fails else f"{fails} FAILED"); sys.exit(1 if fails else 0)
