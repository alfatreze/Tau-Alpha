#!/usr/bin/env python3
"""Direct form I biquad vs trapezoidal (TPT) state-variable filter: sensitivity to coefficient quantisation (B-611, docs/features/CYMO_DSP_REVIEW.md).

The TPT SVF equations are the public ones (Zavalishin, "The Art of VA Filter Design"; Simper/Cytomic, "Solving the continuous SVF equations using trapezoidal integration and
equivalent currents", 2013). The shelf/bell output-mix coefficients are re-derived here and VERIFIED numerically against the cookbook digital response (the float SVF must
equal the RBJ biquad to within 1e-6 dB, since both use the bilinear transform), so nothing depends on any particular implementation of them.
Coefficient-only quantisation (no state rounding noise): the question is how many bits the COEFFICIENTS need at 100-220 Hz. Host only.
"""
import cmath, math, os, sys
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import gen_eq_coeffs as g
import halcyon_model as m
FS = 48000.0

def svf_coeffs(kind, f0, q, gd):
    A = 10 ** (gd / 40.0)
    if kind == "peak":
        gg = math.tan(math.pi * f0 / FS); k = 1.0 / (q * A); m0, m1, m2 = 1.0, k * (A * A - 1), 0.0
    else:
        iq = math.sqrt((A + 1 / A) * (1 / q - 1) + 2)            # shelf-slope S -> 1/Q_eff, as in the cookbook
        k = iq
        if kind == "lowshelf":
            gg = math.tan(math.pi * f0 / FS) / math.sqrt(A); m0, m1, m2 = 1.0, k * (A - 1), A * A - 1
        else:
            gg = math.tan(math.pi * f0 / FS) * math.sqrt(A); m0, m1, m2 = A * A, k * (1 - A) * A, 1 - A * A
    a1 = 1.0 / (1.0 + gg * (gg + k)); a2 = gg * a1; a3 = gg * a2
    return a1, a2, a3, m0, m1, m2

def svf_response_db(c, freqs, n=1 << 16):
    a1, a2, a3, m0, m1, m2 = c
    ic1 = ic2 = 0.0; h = np.zeros(n)
    for i in range(n):
        v0 = 1.0 if i == 0 else 0.0
        v3 = v0 - ic2; v1 = a1 * ic1 + a2 * v3; v2 = ic2 + a2 * ic1 + a3 * v3
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2
        h[i] = m0 * v0 + m1 * v1 + m2 * v2
    H = np.fft.rfft(h); fax = np.arange(len(H)) * FS / n
    out = []
    for f in freqs:
        j = int(round(f * n / FS)); out.append(20 * math.log10(abs(H[j])))
    return np.array(out)

def quant(c, word):
    fa, fm = word - 1, word - 4              # a-coefficients in [0,1]: Q0.(word-1); output mixes up to +-8: Q3.(word-4)
    a1, a2, a3, m0, m1, m2 = c
    qa = lambda v: round(v * (1 << fa)) / (1 << fa); qm = lambda v: round(v * (1 << fm)) / (1 << fm)
    return qa(a1), qa(a2), qa(a3), qm(m0), qm(m1), qm(m2)

if __name__ == "__main__":
    fr = [f for f in g.FREQS if 20 <= f <= 20000]
    # 1. the float SVF equals the cookbook biquad
    worst = 0.0
    for nm, k, f0, q in m.STAGES:
        for gd in (-9.0, 4.5, 9.0):
            ref = np.array([g.response_db([tuple(v * g.QSCALE for v in g.design(k, f0, q, gd))], f) for f in fr])
            got = svf_response_db(svf_coeffs(k, f0, q, gd), fr)
            worst = max(worst, float(np.max(np.abs(ref - got))))
    print("float SVF vs cookbook biquad, worst difference over all stages: %.6f dB" % worst)
    print("\ncoefficient-quantisation response error (dB, vs the unquantised filter), SVF with an 18-bit and a 24-bit word; DF1 biquad figures from eq_coeff_precision.py")
    print("%-10s %-6s %-12s %-12s" % ("stage", "gain", "SVF 18-bit", "SVF 24-bit"))
    for nm, k, f0, q in m.STAGES[:3]:
        for gd in (-9.0, -4.5, 4.5, 9.0):
            c = svf_coeffs(k, f0, q, gd); ref = svf_response_db(c, fr)
            e18 = float(np.max(np.abs(svf_response_db(quant(c, 18), fr) - ref)))
            e24 = float(np.max(np.abs(svf_response_db(quant(c, 24), fr) - ref)))
            print("%-10s %+5.1f  %-12.4f %-12.4f" % (nm, gd, e18, e24))
