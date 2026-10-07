#!/usr/bin/env python3
"""B-608: the EQ coefficient-width finding, as a test (docs/features/CYMO_AUDIO_STACK_REVIEW.md F1).
Proves BOTH halves: the shipped 18-bit Q2.16 coefficients are too coarse for the 100/220 Hz Sound Shaping stages (so the 24-bit option is needed), and
24-bit Q2.22 (+6 extra fractional bits) is accurate and gives uniform, monotonic gain steps."""
import os, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools", "lab")); sys.path.insert(0, os.path.join(ROOT, "tools"))
import eq_coeff_precision as p
import sound_shaping_model as m

fails = 0
def check(name, ok, info=""):
    global fails
    print(("PASS " if ok else "FAIL ") + name + (" " + info if info else ""))
    fails += 0 if ok else 1

gains = (-9.0, -4.5, 4.5, 9.0)
e18 = max(p.worst_err(k, f, q, gd, 0) for nm, k, f, q in m.STAGES[:2] for gd in gains)
e24 = max(p.worst_err(k, f, q, gd, 6) for nm, k, f, q in m.STAGES for gd in gains)
check("18-bit coefficients: the 100/220 Hz stages are off by more than 0.5 dB somewhere (the defect)", e18 > 0.5, "(worst %.3f dB)" % e18)
check("24-bit coefficients: every stage within 0.05 dB of the unrounded design", e24 <= 0.05, "(worst %.4f dB)" % e24)
s18, s24 = p.steps(0), p.steps(6)
check("18-bit: the Bass slider has non-monotonic 0.5 dB steps (the defect)", any(v[2] > 0 for v in s18.values()), str({f: v[2] for f, v in s18.items()}))
check("24-bit: no non-monotonic step at 50/80/120 Hz", all(v[2] == 0 for v in s24.values()))
check("24-bit: steps are uniform (spread at most 0.06 dB at each of 50/80/120 Hz)", all(v[1] - v[0] <= 0.06 for v in s24.values()), str({f: round(v[1] - v[0], 3) for f, v in s24.items()}))
check("18-bit steps are NOT uniform (spread over 0.3 dB at 50 Hz)", s18[50.0][1] - s18[50.0][0] > 0.3)
sys.exit(1 if fails else 0)
