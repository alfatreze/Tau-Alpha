#!/usr/bin/env python3
"""Tests the PROTOTYPE of the mode-overlay safety mechanism (sim/overlay_proto/, docs/features/MODE_OVERLAY_PROPOSAL.md). Not firmware.
1. differential fuzz: the shared region gives the same result as separate buffers for random mode-switch sequences, when each owner re-initialises on claim;
2. the bug class the proposal fears must be CAUGHT: an owner that skips its init reads poison and differs from the separate-buffer reference. (The busy-switch and wrong-owner rules are runtime asserts; they need the real firmware harness and are listed in the proposal, not tested here.)"""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    if not ok: fails += 1
def build(td, name, defs):
    exe = td / name
    r = subprocess.run(["cc", "-O1", "-Wall", "-Wextra", "-Werror", *defs, "-o", str(exe), str(ROOT / "sim/overlay_proto/harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); raise SystemExit("build failed")
    return exe
def run(exe, seed, steps, mode):
    out = subprocess.run([str(exe), str(seed), str(steps), str(mode)], capture_output=True, text=True).stdout.split()
    return int(out[0]), int(out[1])
with tempfile.TemporaryDirectory() as t:
    td = Path(t)
    good = build(td, "good", [])
    same = all(run(good, s, 40000, 0)[0] == run(good, s, 40000, 1)[0] and run(good, s, 40000, 1)[1] == 0 for s in range(1, 41))
    check("overlay == separate buffers over 40 random switch sequences (40,000 steps each), no violations", same)
    skip = build(td, "skip", ["-DMETER_SKIPS_INIT"])
    diff = sum(run(skip, s, 40000, 0)[0] != run(skip, s, 40000, 1)[0] for s in range(1, 41))
    check(f"mutant caught: an owner that skips its init reads poison ({diff} of 40 sequences differ)", diff >= 30)
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
