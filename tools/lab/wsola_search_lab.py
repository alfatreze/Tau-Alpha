#!/usr/bin/env python3
"""Lab for cheaper WSOLA alignment searches (Cymo C7, B-550). Float prototype of the STRUCTURE of the search only, to choose a variant before changing the
fixed-point core: every variant picks each grain's start by normalised cross-correlation, in stages, and is scored on the same real speech by

  mean splice ncc   the correlation, at the full rate over 256 samples, between where the previous grain's tail leads and where the chosen grain starts
                    (one number per splice; the thing the search exists to maximise, measured the same way for every variant)
  splices < 0.5     the share of poor joins
  voicing           mean periodicity of the voiced frames of the OUTPUT (the original is the ceiling)
  MAC / out-s       multiply-adds of the search per second of output (what the CPU pays)

A stage is (decimation, radius): decimate by `dec` (box average), search +-radius candidates (in that stage's units) around the stage's centre, with a window of
N/dec samples; the last stage is at the full rate with its own window length. Usage: python3 tools/lab/wsola_search_lab.py speech.wav [--start 30 --dur 60]
"""
import argparse, sys
import numpy as np
sys.path.insert(0, str(__import__("pathlib").Path(__file__).resolve().parent))
import cymo_tempo_model as m

N, HS = 1024, 512


def box(x, dec):
    n = len(x) // dec
    return x[: n * dec].reshape(n, dec).mean(axis=1)


def ncc_best(sig, ref, lo, hi):
    n = len(ref)
    lo = max(lo, 0); hi = min(hi, len(sig) - n)
    if hi < lo:
        return max(0, min(lo, len(sig) - n)), 0
    win = np.lib.stride_tricks.sliding_window_view(sig, n)[lo:hi + 1]
    num = win @ ref
    en = np.einsum("ij,ij->i", win, win)
    sc = num / np.sqrt(en * float(ref @ ref) + 1e-12)
    k = int(np.argmax(sc))
    return lo + k, (hi - lo + 1) * n


def wsola_staged(x, speed, coarse, refine, fs=44100):
    """coarse: list of (dec, radius, round_to_prev) stages, the first centred on the nominal position with radius in its own units (a 'wide' stage), the later
    ones centred on the previous stage's winner. refine: (window, span) at the full rate, centred on the last coarse winner."""
    L = len(x)
    dsig = {}
    for dec, _ in coarse:
        dsig[dec] = box(x, dec)
    nout = int(L / speed)
    y = np.zeros(nout + 2 * N)
    w = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(N) / N)
    y[:N] += x[:N] * w
    prev, k, macs, nccs = 0, 1, 0, []
    while k * HS < nout and prev + HS + N < L:
        p = max(0, min(int(round(k * HS * speed)), L - N))
        tgt = prev + HS
        centre = None
        for si, (dec, rad) in enumerate(coarse):
            sig = dsig[dec]
            n = N // dec
            ref = sig[tgt // dec: tgt // dec + n]
            c0 = (p // dec) if si == 0 else centre
            pos, mc = ncc_best(sig, ref, c0 - rad, c0 + rad)
            macs += mc
            centre = pos * (dec // (coarse[si + 1][0] if si + 1 < len(coarse) else 1)) if si + 1 < len(coarse) else pos * dec
        win, span = refine
        ref = x[tgt: tgt + win]
        c, mc = ncc_best(x, ref, centre - span, centre + span)
        macs += mc
        c = max(0, min(c, L - N))
        a, b = x[tgt: tgt + 256], x[c: c + 256]
        nccs.append(float(a @ b / np.sqrt((a @ a) * (b @ b) + 1e-12)))
        y[k * HS: k * HS + N] += x[c: c + N] * w
        prev = c
        k += 1
    y = y[: k * HS + N]
    return y, np.array(nccs), macs


def voicing(sig, fs, lo=70, hi=400):
    n = int(0.040 * fs); hop = n // 2; l0, l1 = int(fs / hi), int(fs / lo); v = []
    thr = 0.05 * np.sqrt(np.mean(sig * sig))
    for s in range(0, len(sig) - n - l1, hop):
        f = sig[s:s + n + l1]; a = f[:n]
        if np.sqrt(np.mean(a * a)) < thr:
            continue
        wv = np.lib.stride_tricks.sliding_window_view(f, n)[l0:l1 + 1]
        sc = (wv @ a) / np.sqrt(np.einsum("ij,ij->i", wv, wv) * float(a @ a) + 1e-12)
        v.append(sc.max())
    return float(np.mean(v))


VARIANTS = {
    "V0 current: dec8 +-55, refine 256 +-8":               ([(8, 55)],                  (256, 8)),
    "V1 dec16 +-27, dec8 +-2, refine 256 +-8":             ([(16, 27), (8, 2)],         (256, 8)),
    "V2 dec16 +-27, dec8 +-2, refine 128 +-8":             ([(16, 27), (8, 2)],         (128, 8)),
    "V3 dec16 +-27, dec8 +-2, refine 128 +-4":             ([(16, 27), (8, 2)],         (128, 4)),
    "V4 dec16 +-27, dec4 +-4, refine 128 +-2":             ([(16, 27), (4, 4)],         (128, 2)),
    "V5 dec32 +-14, dec8 +-4, refine 128 +-8":             ([(32, 14), (8, 4)],         (128, 8)),
    "V6 dec16 +-27, refine 128 +-16":                      ([(16, 27)],                 (128, 16)),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav"); ap.add_argument("--start", type=float, default=30.0); ap.add_argument("--dur", type=float, default=60.0)
    ap.add_argument("--speeds", type=float, nargs="+", default=[1.5, 2.0])
    a = ap.parse_args()
    x, fs = m.read_wav(a.wav)
    x = x[int(a.start * fs): int((a.start + a.dur) * fs)]
    print("original voicing %.3f" % voicing(x, fs))
    for sp in a.speeds:
        print("\nspeed %.2fx" % sp)
        print("%-44s %8s %9s %8s %9s" % ("variant", "mean ncc", "ncc<0.5", "voicing", "MAC/out-s"))
        for name, (coarse, refine) in VARIANTS.items():
            y, nc, macs = wsola_staged(x, sp, coarse, refine, fs)
            print("%-44s %8.3f %8.1f%% %8.3f %8.2fM" % (name, nc.mean(), 100 * (nc < 0.5).mean(), voicing(y, fs), macs / (len(y) / fs) / 1e6))


if __name__ == "__main__":
    main()
