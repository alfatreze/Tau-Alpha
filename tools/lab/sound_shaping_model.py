#!/usr/bin/env python3
"""Cymo C5 Sound Shaping host model (docs/features/CYMO_SOUND_SHAPING_SPEC.md): six fixed biquad stages, six perceptual controls, default presets.

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

def coeffs(gains):
    return [tuple(g.quantise(v, "x", []) for v in g.design(k, f, q, gd)) for (_, k, f, q), gd in zip(STAGES, gains)]

def response_db(q, f):
    return g.response_db(q, f)

def analyse(m):
    gs = stage_gains(m)
    q = coeffs(gs)
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

if __name__ == "__main__":
    print("%-11s %-26s  peak   preamp overshoot" % ("preset", "stage gains dB"))
    for name, m, why in PRESETS:
        a = analyse(m)
        print("%-11s %-26s %+5.2f  %+5.2f  %+5.2f   %s" % (name, a["gains"], a["peak"], a["preamp"], a["overshoot"], "stable" if a["stable"] else "UNSTABLE"))
