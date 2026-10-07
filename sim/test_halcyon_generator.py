#!/usr/bin/env python3
"""Halcyon generator checks (D-H02, B-614): analog-matched design, shelf-slope clamp, stability after quantisation at the real width, raw biquad stages.
The coefficient width is chosen by EQ_COEF_BITS at import time, so the width-dependent checks run in child processes."""
import math, os, subprocess, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools", "lab")); sys.path.insert(0, os.path.join(ROOT, "tools"))
if len(sys.argv) > 1 and sys.argv[1] == "child":
    import gen_eq_coeffs as g
    # 40 Hz low shelf +24 dB: the 18-bit format cannot hold a pole this close to z = 1 (the review's instability case); 24 bit can.
    bad = []
    errs = []
    g.quantise_checked(g.design_stage("lowshelf", 40.0, 0.7, 24.0), "40Hz+24", errs)
    bad = [len(errs), 0]
    # a raw biquad (b0 b1 b2 a1 a2) as an imported AutoEQ stage would arrive: unstable input must be refused
    errs = []
    g.quantise_checked((1.0, 0.0, 0.0, -2.1, 1.2), "raw", errs)
    print(g.CW, bad[0] + bad[1], len(errs))
    sys.exit(0)
import gen_eq_coeffs as g
import halcyon_model as m
import numpy as np

fails = 0
def check(name, ok, info=""):
    global fails
    print(("PASS " if ok else "FAIL ") + name + (" " + info if info else ""))
    fails += 0 if ok else 1

def err_vs_analog(co, kind, f0, q, gd):
    worst = 0.0
    for f in g.FREQS:
        z = np.exp(-2j * np.pi * f / g.FS)
        h = (co[0] + co[1] * z + co[2] * z * z) / (1 + co[3] * z + co[4] * z * z)
        worst = max(worst, abs(20 * math.log10(abs(h)) - 20 * math.log10(abs(g.analog_response(kind, f0, q, gd, f)))))
    return worst

cook = match = 0.0
stable_all = True
for nm, k, f0, q in m.STAGES:
    for gd in (-9.0, -4.5, 4.5, 9.0):
        cook = max(cook, err_vs_analog(g.design(k, f0, q, gd), k, f0, q, gd))
        co = g.design_stage(k, f0, q, gd, matched=True)
        match = max(match, err_vs_analog(co, k, f0, q, gd))
        stable_all &= bool(np.all(np.abs(np.roots([1.0, co[3], co[4]])) < 1.0))
check("cookbook strays more than 1 dB from the analog prototype somewhere (the defect)", cook > 1.0, "(worst %.3f dB)" % cook)
check("matched design within 0.08 dB of the analog prototype on every stage and gain", match <= 0.08, "(worst %.3f dB)" % match)
check("every matched stage is stable (unquantised)", stable_all)
check("0 dB stage is the exact cookbook identity (bypass stays bit-exact)", g.design_stage("peak", 1800.0, 0.8, 0.0) == g.design("peak", 1800.0, 0.8, 0.0))
check("shelf slope is clamped to (0, 1]", g.clamp_slope(3.0) == 1.0 and g.clamp_slope(0.0) == 0.1 and g.clamp_slope(0.7) == 0.7)
# a shelf written with S = 2.5 at +-24 dB must still produce a finite, stable design (unclamped, the cookbook square root goes negative)
finite = True
for kind in ("lowshelf", "highshelf"):
    for gd in (-24.0, 24.0):
        co = g.design_stage(kind, 100.0, 2.5, gd)
        finite &= all(math.isfinite(v) for v in co) and bool(np.all(np.abs(np.roots([1.0, co[3], co[4]])) < 1.0))
check("shelf with S = 2.5 at +-24 dB designs finite and stable (clamped)", finite)

res = {}
for bits in (18, 24):
    p = subprocess.run([sys.executable, __file__, "child"], env=dict(os.environ, EQ_COEF_BITS=str(bits)), capture_output=True, text=True)
    res[bits] = p.stdout.split() if p.returncode == 0 else None
check("18-bit: a 40 Hz +24 dB low shelf is refused after quantisation (unstable or out of range)", res[18] is not None and int(res[18][1]) > 0, str(res[18]))
check("24-bit: the same 40 Hz +24 dB low shelf quantises cleanly", res[24] is not None and int(res[24][1]) == 0, str(res[24]))
check("both widths refuse an unstable raw biquad", all(res[b] is not None and int(res[b][2]) > 0 for b in (18, 24)))
t, e = m.build_table(True)
check("default preset table (matched) builds with no range or stability error", not e and len(t["presets"]) == 8, str(e))
pr, e = m.raw_preset("T", [g.design("peak", 1000.0, 1.0, -3.0)], -2.0)
check("raw biquad preset: one stage kept, the other five are exact identity, attenuating preamp accepted", not e and pr["coefs"][1] == [1 << g.QF, 0, 0, 0, 0] and len(pr["coefs"]) == m.NSTAGE)
_, e = m.raw_preset("T", [g.design("peak", 1000.0, 1.0, 3.0)], +2.0)
check("raw biquad preset: a boosting preamp is refused (attenuate-only)", len(e) == 1)
_, e = m.raw_preset("T", [(1, 0, 0, 0, 0)] * 7)
check("raw biquad preset: more than six biquads refused", len(e) == 1)
sys.exit(1 if fails else 0)
