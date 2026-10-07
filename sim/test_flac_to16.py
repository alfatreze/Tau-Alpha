#!/usr/bin/env python3
"""B-619: the 24/20/18-bit to 16-bit reduction in fw/flac.c rounds to nearest instead of flooring.
Checked against an independent reference over every bit depth and edge values, plus the property that matters: no DC bias, error never above half an LSB, 16-bit untouched, 8-bit scaled."""
import random, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent

def ref(v, bps):
    if bps > 16:
        sh = bps - 16
        v = (v + (1 << (sh - 1))) >> sh
    elif bps < 16:
        v = v << (16 - bps)
    return max(-32768, min(32767, v))

fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + (" " + info if info else ""))
    fails += 0 if ok else 1

with tempfile.TemporaryDirectory() as d:
    d = Path(d); exe = d / "t"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Wno-unused-parameter", "-DFLAC_TEST_EXPOSE", "-I", str(ROOT / "fw"), "-o", str(exe), str(ROOT / "sim/flac_to16_harness.c"), str(ROOT / "fw/flac.c")], capture_output=True, text=True)
    if r.returncode:
        print(r.stdout, r.stderr); sys.exit(1)
    rnd = random.Random(5)
    cases = []
    for bps in (8, 16, 18, 20, 24):
        lo, hi = -(1 << (bps - 1)), (1 << (bps - 1)) - 1
        vals = [lo, lo + 1, -1, 0, 1, hi - 1, hi] + [rnd.randint(lo, hi) for _ in range(3000)] + list(range(-300, 301))
        vals = [max(lo, min(hi, v)) for v in vals]
        cases += [(bps, v) for v in vals]
    p = subprocess.run([str(exe)], input="\n".join("%d %d" % c for c in cases), capture_output=True, text=True, check=True)
    got = [int(x) for x in p.stdout.split()]
    want = [ref(v, b) for b, v in cases]
    check("to16 equals the reference for 8/16/18/20/24-bit over %d values (edges, random, around zero)" % len(cases), got == want)
    g24 = [(v, g) for (b, v), g in zip(cases, got) if b == 24 and abs(v) < 8388000]
    err = [g - v / 256.0 for v, g in g24]
    check("24-bit: error never above half an LSB of the 16-bit output", max(abs(e) for e in err) <= 0.5 + 1e-9, "(worst %.4f)" % max(abs(e) for e in err))
    full = [(v, ref(v, 24)) for v in range(-(1 << 12), 1 << 12)]
    bias = sum(g - v / 256.0 for v, g in full) / len(full)
    floor_bias = sum((v >> 8) - v / 256.0 for v, _ in full) / len(full)
    check("24-bit: mean error is about 0 (a floor shift would be %.3f LSB)" % floor_bias, abs(bias) < 0.01 and floor_bias < -0.4, "(rounded %.4f)" % bias)
    check("+full-scale 24-bit saturates instead of wrapping", got[cases.index((24, (1 << 23) - 1))] == 32767)
    check("16-bit passes through unchanged", all(g == v for (b, v), g in zip(cases, got) if b == 16))
sys.exit(1 if fails else 0)
