#!/usr/bin/env python3
"""Cymo C5 Sound Shaping host model (docs/features/CYMO_HALCYON_SPEC.md): six fixed biquad stages, six perceptual controls, default presets.

Uses the project's own coefficient generator (tools/gen_eq_coeffs.py: RBJ cookbook, the same quantiser the RTL model checks), so what is verified here is the real
filter maths. Everything here is DATA that the firmware tables and Tau Omega share; the numbers are the proposed first voicing, to be tuned by ear.

Macro positions are integers -5..+5 (Sibilance 0..+5 = dip depth). Stage gains are in dB, clamped to +-STAGE_MAX, steps of 0.5 dB when applied.
"""
import math, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import gen_eq_coeffs as g

STAGES = [                       # (name, kind, centre Hz, Q)
    ("low shelf",   "lowshelf",   100.0, 0.70),
    ("body",        "peak",       220.0, 1.00),
    ("vocal",       "peak",      1800.0, 0.80),
    ("punch",       "peak",      3500.0, 1.20),
    ("sibilance",   "peak",      6500.0, 1.50),
    ("air",         "highshelf", 10000.0, 0.70),
]
NSTAGE = len(STAGES)
STAGE_MAX = 9.0
MACROS = ("warmth", "bass", "vocal", "punch", "sibilance", "air")   # warmth is bipolar (+ warm, - clear); sibilance is 0..5

def clamp_slope(S):
    """Shelf stages use the cookbook's shelf-SLOPE form (the 0.70 in STAGES is S, not Q). That form's square root goes negative, and the design fails, for S > 1 once the
    gain is large enough, and S <= 1 is exactly the monotonic (no resonant overshoot) range. Any tool that edits a shelf stage (Tau Omega's EQST) must clamp to (0, 1]."""
    return max(0.1, min(1.0, S))

def stage_gains(m):
    """dB per stage from the six control positions (the stage gain is the clamped sum of what each control contributes)."""
    w, b, v, p, s, a = (m[k] for k in MACROS)
    gs = [1.0 * b + 0.5 * w,         # low shelf: bass weight, plus the warm side of the tilt
          0.8 * w,                   # body: warmth adds, clarity removes mud
          0.9 * v,                   # vocal presence
          0.9 * p,                   # punch / definition
          -1.0 * s,                  # sibilance: dip only
          0.9 * a - 0.5 * w]         # air, plus the clear side of the tilt
    return [max(-STAGE_MAX, min(STAGE_MAX, round(x * 2) / 2)) for x in gs]   # 0.5 dB steps

def coeffs(gains, matched=False):
    """Quantised coefficients at the build's width. matched=True uses the offline analog-matched design (gen_eq_coeffs.design_matched, D-H02); the default stays the
    cookbook so the shipped, bit-exact-tested paths do not change."""
    return [tuple(g.quantise(v, "x", []) for v in g.design_stage(k, f, q, gd, matched)) for (_, k, f, q), gd in zip(STAGES, gains)]

def response_db(q, f):
    return g.response_db(q, f)

def analyse(m, matched=False):
    gs = stage_gains(m)
    q = coeffs(gs, matched)
    peak = max(response_db(q, f) for f in g.FREQS)
    pre = min(0.0, g.loudness_preamp_db(q)) if any(gs) else 0.0   # attenuate-only, like ReplayGain: a loudness-matching boost on a cut-heavy preset would only eat headroom
    return dict(gains=gs, q=q, peak=peak, preamp=pre, overshoot=peak + pre, stable=g.stable(q))

ZERO = dict(warmth=0, bass=0, vocal=0, punch=0, sibilance=0, air=0)
def P(**kw): return dict(ZERO, **kw)

# Default presets, rebuilt from the controls (name, tuple, why)
PRESETS = [
    ("FLAT",       P(),                                                         "reference: all controls at zero is a bit-exact bypass"),
    ("WARM",       P(warmth=+3, bass=+1),                                       "fuller body, softer top: mild low-mid and bass lift, a gentle air roll-off (tilt)"),
    ("CLEAR",      P(warmth=-3, punch=+1, air=+1),                              "less low-mid mud, more definition and air, for dull or muddy masters"),
    ("BASS",       P(warmth=+1, bass=+4, punch=+1),                             "small-driver bass weight; the auto-preamp gives the headroom back"),
    ("VOCAL",      P(bass=-1, vocal=+3, sibilance=+1),                          "presence in the 1-3 kHz speech band, less boom, a touch of sibilance control"),
    ("SPEECH",     P(warmth=-1, bass=-3, vocal=+3, punch=+1, sibilance=+2),     "audiobooks and podcasts: cut rumble and plosive energy, intelligibility band up, sibilance down"),
    ("LOW VOLUME", P(warmth=+1, bass=+5, air=+2, sibilance=+1),                 "equal-loudness compensation (ISO 226 trend): the ear loses bass and top at low level"),
    ("SMOOTH",     P(punch=-2, sibilance=+2, air=-2),                           "fatigue reduction for harsh or bright recordings: eased presence, sibilance and top"),
]

def build_table(matched=True):
    """The preset table as DATA (what Tau Omega's PRST/EQST sections and the firmware loader share): per preset the six stage gains, the quantised stage coefficients
    at the build's width (run with EQ_COEF_BITS=24 for Q2.22), and the attenuate-only preamp. Every stage is checked with gen_eq_coeffs.quantise_checked (range and quantised poles)."""
    out, errs = [], []
    for name, mc, why in PRESETS:
        a = analyse(mc, matched)
        for (sn, k, f, q), c, gd in zip(STAGES, a["q"], a["gains"]):
            if not g.stable([c]):
                errs.append("%s/%s: unstable after quantisation to Q2.%d" % (name, sn, g.QF))
        out.append(dict(name=name, gains=a["gains"], coefs=[list(c) for c in a["q"]], preamp_db=a["preamp"], overshoot_db=a["overshoot"]))
    return dict(width=g.CW, frac_bits=g.QF, matched=matched, stages=[dict(name=n, kind=k, f0=f, q=q) for n, k, f, q in STAGES], presets=out), errs

def raw_preset(name, biquads, preamp_db=0.0):
    """A preset that carries real biquads (b0 b1 b2 a1 a2, floats, up to NSTAGE) instead of control positions: the form an AutoEQ / Equalizer APO import produces
    (Tau Omega converts PK / LSC / HSC with design_stage). Each is quantised and checked; returns (preset dict, errors)."""
    errs = []
    if len(biquads) > NSTAGE:
        errs.append("%s: %d biquads, at most %d" % (name, len(biquads), NSTAGE))
    cs = [list(g.quantise_checked(b, "%s/%d" % (name, i), errs)) for i, b in enumerate(biquads[:NSTAGE])]
    cs += [[1 << g.QF, 0, 0, 0, 0]] * (NSTAGE - len(cs))      # unused stages are identity (b0 = 1.0), which is exact in the DF1 recursion
    if preamp_db > 0:
        errs.append("%s: preamp %+.2f dB (attenuate-only, like ReplayGain)" % (name, preamp_db))
    return dict(name=name, gains=None, coefs=cs, preamp_db=min(0.0, preamp_db)), errs

if __name__ == "__main__":
    if "--json" in sys.argv:          # EQ_COEF_BITS=24 python3 tools/lab/halcyon_model.py --json > halcyon_presets_q22.json
        import json
        t, e = build_table(True)
        if e: sys.exit("\n".join(e))
        print(json.dumps(t, indent=1)); sys.exit(0)
    print("%-11s %-26s  peak   preamp overshoot" % ("preset", "stage gains dB"))
    for name, m, why in PRESETS:
        a = analyse(m)
        print("%-11s %-26s %+5.2f  %+5.2f  %+5.2f   %s" % (name, a["gains"], a["peak"], a["preamp"], a["overshoot"], "stable" if a["stable"] else "UNSTABLE"))
