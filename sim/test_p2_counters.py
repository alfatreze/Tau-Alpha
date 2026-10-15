#!/usr/bin/env python3
"""Player 2 counter registry (fw2/cores/counters_core.h): the exported report entries equal what the registry holds -- every counter, no cut, signed and unsigned, the unit --
and the mutants (the last counter dropped, the old 28-character cut, a prefix lookup, a wrong width, an unsigned printed negative) are killed."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    if not ok: fails += 1
WANT = ["E 0 RING FILL|48672 PAIRS", "E 1 A8|200", "E 2 B16|65535", "E 3 BIG U32|3000000000", "E 4 NEG I32|-2147483648", "E 5 LONG VALUE|4294967295 PERMILLE", "E 6 LAST|7 MS", "E 7 TEXT|0.6.0-dev.126+b31f522.dirty FLAC 44.1K", "E 8 CORE VERSION|4D50331A"]
def run(d, bug):
    exe = d / f"c{bug}"
    r = subprocess.run(["cc", "-O1", "-Wall", "-Werror", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_counters_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return subprocess.run([str(exe)], capture_output=True, text=True).stdout.splitlines()
def good(out): return out[:9] == WANT and out[9] == "N 9" and out[10] == "F 6 -1 -1"
with tempfile.TemporaryDirectory() as td:
    d = Path(td)
    check("every registered counter is exported with its full value and unit; lookups are exact", good(run(d, 0)))
    check("a 38-character text value and a 19-character numeric value are not cut", "E 7 TEXT|0.6.0-dev.126+b31f522.dirty FLAC 44.1K" in run(d, 0) and "E 5 LONG VALUE|4294967295 PERMILLE" in run(d, 0))
    for b, nm in {1: "the last counter is never exported", 2: "values are cut at 28 characters (the old export)", 3: "a name prefix matches a lookup", 4: "a 16-bit counter reads its neighbour's bytes", 5: "an unsigned counter above 2^31 prints negative"}.items():
        out = run(d, b)
        check(f"mutant caught: {nm}", not good(out))
print("PASSED" if not fails else f"{fails} FAILED"); sys.exit(1 if fails else 0)
