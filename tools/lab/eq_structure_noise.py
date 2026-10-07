#!/usr/bin/env python3
"""ROUNDING noise (not response error) of the two EQ structures at low frequency: DF1 biquad (24-bit coefficients, as the engine would run) vs TPT SVF (18-bit coefficients) (B-611).

Both run on integer state with 16 fractional bits and round to nearest after each accumulate, the way the engine does. The error is measured against a double-precision
version of the SAME structure on the same input, so it is the arithmetic (rounding) noise, not the response difference. Input: 1 kHz sine at -40 dBFS plus 100 Hz at -20 dBFS,
through the 100 Hz low shelf and the 220 Hz bell at +9 dB. Host only (numpy).
"""
import math, os, sys
os.environ["EQ_COEF_BITS"] = "24"
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, ".."))
import gen_eq_coeffs as g
import halcyon_model as m
import eq_svf_precision as sv
FS = 48000.0; N = 48000
def rs(v, n): return (v + (1 << (n - 1))) >> n
def drive():
    t = np.arange(N) / FS
    return np.round(32767 * (0.01 * np.sin(2 * np.pi * 1000 * t) + 0.1 * np.sin(2 * np.pi * 100 * t))).astype(np.int64)
def df1(kind, f0, q, gd, x):
    c = g.design(kind, f0, q, gd); qc = [int(round(v * g.QSCALE)) for v in c]; FC = g.QF
    b0, b1, b2, a1, a2 = qc; x1 = x2 = y1 = y2 = 0; out = np.zeros(len(x), dtype=np.int64)
    X = x << 16
    for i, xi in enumerate(X):
        acc = b0 * int(xi) + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
        y = rs(acc, FC); x2, x1 = x1, int(xi); y2, y1 = y1, y; out[i] = y
    ref = np.zeros(len(x)); bf, af = [v / g.QSCALE for v in qc[:3]], [v / g.QSCALE for v in qc[3:]]   # same QUANTISED coefficients in double precision: isolates rounding noise
    x1f = x2f = y1f = y2f = 0.0
    for i, xi in enumerate(x):
        yf = bf[0] * xi + bf[1] * x1f + bf[2] * x2f - af[0] * y1f - af[1] * y2f
        x2f, x1f, y2f, y1f = x1f, float(xi), y1f, yf; ref[i] = yf
    return out / 65536.0, ref
def svf(kind, f0, q, gd, x, word=18):
    c = sv.svf_coeffs(kind, f0, q, gd); fa, fm = word - 1, word - 4
    a1, a2, a3 = [int(round(v * (1 << fa))) for v in c[:3]]; m0, m1, m2 = [int(round(v * (1 << fm))) for v in c[3:]]
    ic1 = ic2 = 0; out = np.zeros(len(x))
    for i, xi in enumerate(x):
        v0 = int(xi) << 16; v3 = v0 - ic2
        v1 = rs(a1 * ic1 + a2 * v3, fa); v2 = ic2 + rs(a2 * ic1 + a3 * v3, fa)
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2
        out[i] = rs(m0 * v0 + m1 * v1 + m2 * v2, fm) / 65536.0
    a1f, a2f, a3f = [v / (1 << fa) for v in (a1, a2, a3)]; m0f, m1f, m2f = [v / (1 << fm) for v in (m0, m1, m2)]; ic1 = ic2 = 0.0; ref = np.zeros(len(x))   # quantised coefficients, double precision
    for i, xi in enumerate(x):
        v3 = xi - ic2; v1 = a1f * ic1 + a2f * v3; v2 = ic2 + a2f * ic1 + a3f * v3
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; ref[i] = m0f * xi + m1f * v1 + m2f * v2
    return out, ref
if __name__ == "__main__":
    x = drive(); tail = slice(4800, None)
    print("rounding-noise RMS error in LSB of the 16-bit output (lower is better), 4,800-sample warm-up skipped")
    print("%-10s %-6s %-22s %-22s" % ("stage", "gain", "DF1, 24-bit coef", "SVF, 18-bit coef"))
    for nm, k, f0, q in m.STAGES[:2]:
        for gd in (4.5, 9.0):
            a, ar = df1(k, f0, q, gd, x); b, br = svf(k, f0, q, gd, x)
            ea = float(np.sqrt(np.mean((a[tail] - ar[tail]) ** 2))); eb = float(np.sqrt(np.mean((b[tail] - br[tail]) ** 2)))
            print("%-10s %+5.1f  %-22.5f %-22.5f" % (nm, gd, ea, eb))
