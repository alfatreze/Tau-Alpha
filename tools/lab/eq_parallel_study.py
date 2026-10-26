#!/usr/bin/env python3
"""Cascade (shipped plan) vs PARALLEL weighted-band-pass EQ for the six Sound Shaping stages (B-611, docs/features/CYMO_DSP_REVIEW.md).

Parallel form:  y = x + sum_i w_i * F_i(x),  F_i = a FIXED unity-peak filter (low-pass for the low shelf, band-passes for the bells, high-pass for the high shelf), w_i = gain_i - 1.
Every control becomes one multiplier on a fixed filter's output: no coefficient tables, no coefficient quantisation at the controls, click-free per-sample gain ramps, a dynamic band
is just a time-varying w_i. This script asks how closely six such bands can reproduce the CASCADE responses of the default presets (weights fitted by least squares on a 1/24-octave
log grid; magnitude only). The identity 'a peaking filter = 1 + (g-1) * unity-peak band-pass' is textbook (it is also how parallel dynamic bands are built); only fixed-frequency bands
at the Sound Shaping centres are used. Host only (numpy). No third-party code.
"""
import math, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, ".."))
import gen_eq_coeffs as g
import halcyon_model as m
FS = 48000.0
fr = np.array(g.FREQS); z = np.exp(-2j * np.pi * fr / FS)
def bq(b, a): return (b[0] + b[1] * z + b[2] * z * z) / (a[0] + a[1] * z + a[2] * z * z)
def lowpass(f0, q):
    w = 2 * math.pi * f0 / FS; al = math.sin(w) / (2 * q); c = math.cos(w)
    return bq(((1 - c) / 2, 1 - c, (1 - c) / 2), (1 + al, -2 * c, 1 - al))
def highpass(f0, q):
    w = 2 * math.pi * f0 / FS; al = math.sin(w) / (2 * q); c = math.cos(w)
    return bq(((1 + c) / 2, -(1 + c), (1 + c) / 2), (1 + al, -2 * c, 1 - al))
def bandpass(f0, q):                       # constant 0 dB peak gain
    w = 2 * math.pi * f0 / FS; al = math.sin(w) / (2 * q); c = math.cos(w)
    return bq((al, 0.0, -al), (1 + al, -2 * c, 1 - al))
# fixed bank at the Sound Shaping centres (shelves as 2nd-order Butterworth-like low/high-pass, bells as band-passes of the stage Q)
BANK = [lowpass(100.0, 0.707), bandpass(220.0, 1.0), bandpass(1800.0, 0.8), bandpass(3500.0, 1.2), bandpass(6500.0, 1.5), highpass(10000.0, 0.707)]
def parallel_db(w): return 20 * np.log10(np.abs(1 + sum(wi * Fi for wi, Fi in zip(w, BANK))))
def fit(target_db, w0):
    w = np.array(w0, float); lam = 1e-3
    res = lambda w: parallel_db(w) - target_db
    for _ in range(80):
        r = res(w); J = np.empty((len(r), 6))
        for i in range(6):
            d = np.zeros(6); d[i] = 1e-6; J[:, i] = (res(w + d) - r) / 1e-6
        A = J.T @ J
        try: step = np.linalg.solve(A + lam * np.diag(np.diag(A)) + 1e-12 * np.eye(6), -J.T @ r)
        except np.linalg.LinAlgError: break
        if np.sum(res(w + step) ** 2) < np.sum(r ** 2): w = w + step; lam *= 0.5
        else: lam *= 4
    return w, float(np.max(np.abs(res(w)))), float(np.sqrt(np.mean(res(w) ** 2)))
if __name__ == "__main__":
    print("%-11s %-34s %-9s %-9s  fitted weights w_i (gain-1 per band)" % ("preset", "cascade stage gains (dB)", "worst dB", "rms dB"))
    worst_all = 0
    for name, c, _ in m.PRESETS[1:]:
        gs = m.stage_gains(c); q = m.coeffs(gs)
        target = np.array([g.response_db(q, f) for f in fr])
        w0 = [10 ** (x / 20) - 1 for x in gs]
        w, wmax, rms = fit(target, w0); worst_all = max(worst_all, wmax)
        print("%-11s %-34s %-9.3f %-9.3f  %s" % (name, str(gs), wmax, rms, " ".join("%+.2f" % x for x in w)))
    # single-control extremes (one band at +-9 dB, the rest flat): the identity should be near exact
    print("\nsingle band at +-9 dB (others flat), worst dB error of the parallel form against the cascade")
    for i, (nm, *_ ) in enumerate(m.STAGES):
        row = []
        for gd in (-9.0, 9.0):
            gs = [0.0] * 6; gs[i] = gd; q = m.coeffs(gs); target = np.array([g.response_db(q, f) for f in fr])
            w, wmax, _ = fit(target, [10 ** (x / 20) - 1 for x in gs]); row.append("%+.0f dB: %.3f" % (gd, wmax))
        print("  %-10s %s" % (nm, "   ".join(row)))
