#!/usr/bin/env python3
"""Host check (B-291) for docs/MP3_FILTERBANK_KERNEL_DESIGN.md: builds sim/mp3_poly_probe.c against Helix's REAL dct32.c, polyphase.c and
trigtabs.c (copied to a temp dir so the portable sim/helix_host/assembly.h shim is picked up; the operations are bit-identical to the RISC-V build)
and asserts what the hardware window unit relies on: FDCT32 emits 32 unique words per call, the window reads only words of the last 16 slots of
the same channel, every one of a slot's 32 words is read at some age, and the per-call working set is 263 words."""
import shutil, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
H = ROOT / "third_party/libhelix-mp3"
fails = 0
def check(name, ok, detail=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + detail))
    if not ok: fails += 1
with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    for f in ("dct32.c", "polyphase.c", "trigtabs.c"): shutil.copy(H / "real" / f, td / f)
    shutil.copy(ROOT / "sim/helix_host/assembly.h", td / "assembly.h")
    exe = td / "probe"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-I", str(td), "-I", str(H / "pub"), "-I", str(H / "real"), "-o", str(exe),
                        str(ROOT / "sim/mp3_poly_probe.c"), str(td / "dct32.c"), str(td / "polyphase.c"), str(td / "trigtabs.c")],
                       capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    p = subprocess.run([str(exe)], capture_output=True, text=True)
    out = p.stdout
    check("no window read falls outside the last slots' FDCT32 words", p.returncode == 0 and "violations: 0" in out, out)
    check("FDCT32 emits 32 unique words per call", "max 32 (L), 32 (R)" in out.split("\n")[0], out)
    check("the window reads 263 distinct words per call per channel", "max 263 (L), 263 (R)" in out, out)
    check("history depth is 16 slots (oldest age 15)", "oldest age used: 15 slots" in out, out)
    check("every word of a slot is read at some age (store all 32)", "min 32 / max 32 (L), min 32 / max 32 (R)" in out, out)
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
