#!/usr/bin/env python3
"""stream_wait_idle(): waiting out an in-flight read must not leave a never-issued request marked in flight (the Rite of Spring lock: ST TIMEOUTS 1, nothing read again).
Mutant 60 is the old quiesce (the pump's result ignored): the stream stays 'busy' forever."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    if not ok: fails += 1
def run(bug):
    with tempfile.TemporaryDirectory() as td:
        exe = Path(td) / "h"
        r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_stream_idle_harness.c")], capture_output=True, text=True)
        if r.returncode: print(r.stderr); sys.exit(1)
        return subprocess.run([str(exe)], capture_output=True, text=True).stdout.split("\n")
o = run(0)
check("the read in flight is waited out and the stream goes idle", o[0].startswith("IDLE steps=") and o[0] != "IDLE steps=0")
check("afterwards the normal pump issues the next read and collects it", o[1] == "NEXT ok" and o[2].startswith("TIMEOUTS 0 READS 2"))
m = run(60)
check("mutant 60 (the pump's request is left marked in flight) is caught", m[0] == "IDLE steps=0" or m[1] != "NEXT ok")
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
