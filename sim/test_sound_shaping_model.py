#!/usr/bin/env python3
"""Properties the Sound Shaping model must have before any RTL or firmware exists (docs/features/CYMO_SOUND_SHAPING_SPEC.md)."""
import math, os, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools", "lab")); sys.path.insert(0, os.path.join(ROOT, "tools"))
import sound_shaping_model as m
import gen_eq_coeffs as g

fails = 0
def check(name, ok, info=""):
    global fails
    print(("PASS " if ok else "FAIL ") + name + (" " + info if info else ""))
    fails += 0 if ok else 1

check("six stages", m.NSTAGE == 6)
check("all controls at zero: every stage gain is 0 (the firmware then selects the bit-exact bypass)", m.stage_gains(m.ZERO) == [0.0] * 6)
allm = [dict(m.ZERO, **{k: v}) for k in m.MACROS for v in ((range(0, 6) if k == "sibilance" else range(-5, 6)))]
check("stage gains are in 0.5 dB steps and within +-9 dB for every single-control position",
      all(abs(x) <= m.STAGE_MAX and abs(x * 2 - round(x * 2)) < 1e-9 for c in allm for x in m.stage_gains(c)))
ext = [dict(zip(m.MACROS, t)) for t in [(5,5,5,5,5,5), (-5,-5,-5,-5,5,-5), (5,5,-5,5,0,-5), (-5,5,5,-5,5,5)]]
check("clamp holds at the extremes", all(abs(x) <= m.STAGE_MAX for c in ext for x in m.stage_gains(c)))

# every preset: stable, coefficients inside the 18-bit range, overshoot bounded, preamp never boosts
ok_stable = ok_range = ok_over = ok_pre = True; worst = -99
for name, c, _ in m.PRESETS:
    a = m.analyse(c)
    ok_stable &= bool(a["stable"])
    ok_range &= all(g.QMIN <= v <= g.QMAX for st in a["q"] for v in st)
    ok_over &= a["overshoot"] <= 4.0
    ok_pre &= a["preamp"] <= 0.0
    worst = max(worst, a["overshoot"])
check("all default presets stable", ok_stable)
check("all coefficients inside the 18-bit signed range", ok_range)
check("overshoot after the preamp is at most +4 dB (the EQ input range is +12 dB, the soft clipper covers the rest)", ok_over, "(worst %+.2f dB)" % worst)
check("the auto-preamp never boosts", ok_pre)
check("FLAT is the first preset and is all zero", m.PRESETS[0][0] == "FLAT" and m.PRESETS[0][1] == m.ZERO)
check("eight default presets with distinct names", len(m.PRESETS) == 8 and len({p[0] for p in m.PRESETS}) == 8)

# cascade dB additivity (what makes the controls composable)
c = dict(warmth=2, bass=3, vocal=-2, punch=4, sibilance=3, air=-3)
gs = m.stage_gains(c); q = m.coeffs(gs)
err = 0.0
for f in g.FREQS:
    s = sum(m.response_db(m.coeffs([x if i == j else 0 for j in range(6)]), f) for i, x in enumerate(gs))
    err = max(err, abs(m.response_db(q, f) - s))
check("cascade response equals the sum of the stage responses in dB", err < 0.01, "(worst %.4f dB)" % err)

# each control moves the response at its own stage's centre the right way
def at(cm, f): return m.response_db(m.coeffs(m.stage_gains(cm)), f)
check("Bass up raises 100 Hz, down lowers it", at(dict(m.ZERO, bass=3), 100) > 1.0 > -1.0 > at(dict(m.ZERO, bass=-3), 100))
check("Vocal up raises 1.8 kHz", at(dict(m.ZERO, vocal=3), 1800) > 1.5)
check("Punch up raises 3.5 kHz", at(dict(m.ZERO, punch=3), 3500) > 1.5)
check("Sibilance lowers 6.5 kHz and leaves 200 Hz alone", at(dict(m.ZERO, sibilance=4), 6500) < -2.0 and abs(at(dict(m.ZERO, sibilance=4), 200)) < 0.3)
check("Air up raises 12 kHz", at(dict(m.ZERO, air=3), 12000) > 1.0)
check("Warmth is a tilt: warm raises 200 Hz and lowers 12 kHz, clear does the opposite", at(dict(m.ZERO, warmth=4), 200) > 1.0 > -1.0 > at(dict(m.ZERO, warmth=4), 12000) and at(dict(m.ZERO, warmth=-4), 200) < -1.0 and at(dict(m.ZERO, warmth=-4), 12000) > 1.0)

# cost figures quoted in the spec
per_stage_clocks = 116 / 5.0
check("six stages: about 139 clocks per sample, 10% of 1,388 at 66.7 MHz", abs(per_stage_clocks * 6 - 139) < 1 and per_stage_clocks * 6 / 1388 < 0.11, "(%.0f clocks, %.1f%%)" % (per_stage_clocks * 6, per_stage_clocks * 6 / 13.88))
bits = 6 * 37 * 5 * 18
check("gain-indexed table (37 steps x 5 coefficients x 6 stages x 18 bits) is about 20 kb, two M10K blocks (or MLAB)", bits == 19980 and bits < 10240 * 2, "(%d bits)" % bits)
sys.exit(1 if fails else 0)
