#!/usr/bin/env python3
"""B-631: fw/halcyon_core.h (the real C, compiled here) against tools/lab/halcyon_model.py at Q2.22 with the analog-matched design: control positions to stage steps, the table
coefficients (exact integers), and the peak-safe preamp (never below the exact peak of the quantised cascade, at most 0.8 dB above it). Also: the generated table is fresh."""
import math, os, random, subprocess, sys, tempfile
os.environ["EQ_COEF_BITS"] = "24"
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools")); sys.path.insert(0, str(ROOT / "tools" / "lab"))
import gen_eq_coeffs as g
import halcyon_model as m
import gen_halcyon_tab as tab

fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + (" " + info if info else ""))
    fails += 0 if ok else 1

check("fw/halcyon_tab.h is up to date with tools/gen_halcyon_tab.py", (ROOT / "fw/halcyon_tab.h").read_text() == tab.render())
rnd = random.Random(11)
sets = [dict(p[1]) for p in m.PRESETS]
sets += [{k: (rnd.randint(0, 5) if k == "sibilance" else rnd.randint(-5, 5)) for k in m.MACROS} for _ in range(300)]
sets += [dict(m.ZERO, **{k: v}) for k in m.MACROS for v in ((0, 5) if k == "sibilance" else (-5, 5))]
with tempfile.TemporaryDirectory() as d:
    exe = Path(d) / "h"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-I", str(ROOT / "fw"), "-o", str(exe), str(ROOT / "sim/halcyon_core_harness.c")], capture_output=True, text=True)
    if r.returncode:
        print(r.stdout, r.stderr); sys.exit(1)
    inp = "\n".join(" ".join(str(c[k]) for k in m.MACROS) for c in sets)
    out = subprocess.run([str(exe)], input=inp, capture_output=True, text=True, check=True).stdout.splitlines()
    step_bad = coef_bad = pre_bad = 0
    over, under = [], []
    for c, line in zip(sets, out):
        v = list(map(int, line.split()))
        steps, coefs, n, pre = v[:6], v[6:36], v[36], v[37]
        gains = m.stage_gains(c)
        want_steps = [int(round(x * 2)) + 18 for x in gains]
        if steps != want_steps:
            step_bad += 1; continue
        q = m.coeffs(gains, True)
        if [x for st in q for x in st] != coefs:
            coef_bad += 1
        # exact peak boost of the quantised cascade on a fine grid
        peak = max(g.response_db(q, f) for f in [20 * (1000.0 ** (i / 800.0)) for i in range(801)])
        atten = n / 8.0
        need = max(peak, 0.0)
        if atten < need - 0.02: pre_bad += 1
        over.append(atten - need)
        if pre != int(round(10 ** (-n / 8.0 / 20.0) * (1 << 22))): pre_bad += 1
    check("control positions give the same stage steps as the model for %d settings" % len(sets), step_bad == 0, "(%d differ)" % step_bad)
    check("table coefficients equal the model's quantised matched design exactly", coef_bad == 0, "(%d settings differ)" % coef_bad)
    check("the preamp never gives back less than the exact peak boost (within 0.02 dB)", pre_bad == 0, "(%d too small)" % pre_bad)
    check("and is conservative by at most 0.8 dB (worst %.2f dB, mean %.2f dB)" % (max(over), sum(over) / len(over)), max(over) <= 0.8)
    flat = [i for i, c in enumerate(sets) if all(x == 0 for x in c.values())][0]
    fl = list(map(int, out[flat].split()))
    check("FLAT (all controls zero): unity preamp", fl[36] == 0 and fl[37] == 1 << 22)
sys.exit(1 if fails else 0)
