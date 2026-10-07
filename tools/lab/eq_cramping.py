#!/usr/bin/env python3
"""How far does the bilinear-transform (RBJ cookbook) response of the Sound Shaping stages stray from the analog prototype it is meant to imitate? (B-611)

The cookbook designs (tools/gen_eq_coeffs.py) pre-warp the centre frequency but the response still 'cramps' toward Nyquist. This prints, per stage and gain, the worst
dB difference between the digital response (unquantised) and the analog prototype H(s) over a few frequency ranges. Host only; the analog prototypes are the standard
RBJ Audio EQ Cookbook ones. Used to decide whether an analog-matched design (Vicanek-style) is worth it at fs = 48 kHz.
"""
import cmath, math, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import gen_eq_coeffs as g
import halcyon_model as m
FS = 48000.0
def analog(kind, f0, q, gd, f):
    A = 10 ** (gd / 40.0); s = 1j * f / f0
    if kind == "peak":
        return (s*s + s*(A/q) + 1) / (s*s + s/(A*q) + 1)
    r = math.sqrt(A)
    # our shelves use the cookbook's shelf-SLOPE form: the 'q' in the stage table is S (0.7), and 1/Q_eff = sqrt((A + 1/A)(1/S - 1) + 2)
    iq = math.sqrt((A + 1.0 / A) * (1.0 / q - 1.0) + 2.0)
    if kind == "lowshelf":
        return A * (s*s + (r*iq)*s + A) / (A*s*s + (r*iq)*s + 1)
    return A * (A*s*s + (r*iq)*s + 1) / (s*s + (r*iq)*s + A)
def digital(kind, f0, q, gd, f):
    b0, b1, b2, a1, a2 = g.design(kind, f0, q, gd)
    z = cmath.exp(-2j * math.pi * f / FS)
    return (b0 + b1*z + b2*z*z) / (1 + a1*z + a2*z*z)
def db(h): return 20 * math.log10(abs(h))
bands = [("to 8 kHz", [f for f in g.FREQS if f <= 8000]), ("8-16 kHz", [f for f in g.FREQS if 8000 < f <= 16000]), ("16-20 kHz", [f for f in g.FREQS if f > 16000])]
if __name__ == "__main__":
    print("worst |digital - analog| in dB, unquantised, fs = 48 kHz")
    print("%-10s %-6s %s" % ("stage", "gain", "  ".join("%-10s" % b[0] for b in bands)))
    for nm, k, f0, q in m.STAGES:
        for gd in (-9.0, -4.5, 4.5, 9.0):
            row = ["%.2f" % max(abs(db(digital(k, f0, q, gd, f)) - db(analog(k, f0, q, gd, f))) for f in fr) for _, fr in bands]
            print("%-10s %+5.1f  %s" % (nm, gd, "  ".join("%-10s" % x for x in row)))
