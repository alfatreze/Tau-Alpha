#!/usr/bin/env python3
"""True-peak limiter host model (parallel plan A8, B-622; prepared for gate G-ISP, docs/features/CYMO_RECORDING_PLAN_DEV105.md section 6).

WHY. A sample-domain soft clipper (tools/lab/cymo_out_model.softclip, knee 0.9 FS) bounds the SAMPLES but not the reconstructed waveform: a hot master still has inter-sample peaks
above full scale (+1.8 dB on the hot proxy used here). If the recording plan finds the converter or amp clips inter-sample overs, the ceiling has to bound the TRUE peak, which needs a detector
that sees between the samples and a gain that is already down when the peak arrives. This is that design on the host, with the numbers that decide whether it is worth building.

DESIGN (linked stereo, one gain for both channels so the image does not move):
  1. detector: 4x oversampled peak (polyphase windowed-sinc, 16 taps per phase) of |L| and |R|, the maximum per sample interval;
  2. gain target gt[n] = min(1, ceiling / peak[n]);
  3. look-ahead N samples: gm[m] = min(gt[m-N .. m]); release: gr[m] = min(gm[m], gr[m-1] + (1 - gr[m-1]) * (1 - exp(-1/(release_s * fs))));
     attack smoothing: gs[m] = mean(gr[m-N .. m]) (a boxcar over N+1 samples, so the gain is already at its minimum when the peak arrives and never steps);
  4. output y[m] = x[m-N] * gs[m]  (the signal is delayed by N samples; N = 32 is 0.67 ms at 48 kHz).
Below the ceiling gs is exactly 1 and the output is the delayed input unchanged (bit-exact after rounding). Per sample cost [EST]: the detector is 4 x 16 MACs per channel (or a shorter
polyphase), a running min over N, a box mean (running sum) and one multiply per channel: affordable on the time-multiplexed engine, but it is a real block: the decision is the owner's after G-ISP.

    python3 tools/lab/cymo_tplimit_model.py selftest
"""
import math
import sys

import numpy as np

FS = 48000.0
N_LOOK = 32
RELEASE_S = 0.060
TAPS = 16
OS = 4


def _phase_filters(taps=TAPS, os_=OS):
    """Polyphase interpolation filters for fractional delays k/os (k = 1..os-1), Hann-windowed sinc."""
    h = []
    for k in range(1, os_):
        frac = k / float(os_)
        j = np.arange(-taps // 2 + 1, taps // 2 + 1)        # taps around the interval
        t = j - frac
        w = 0.5 * (1 + np.cos(np.pi * t / (taps / 2 + 1)))
        h.append(np.sinc(t) * w)
    return h


_H = _phase_filters()


def true_peak(x):
    """Estimated true peak per sample interval n..n+1 (max of the sample and the three interpolated points)."""
    x = np.asarray(x, float)
    p = np.abs(x)
    for h in _H:
        # y[n + k/os] = sum_j h[j] * x[n + j + (1 - taps/2) ...]: use a plain convolution aligned so that index n holds the point between n and n+1
        y = np.convolve(x, h[::-1], mode='full')[len(h) // 2:len(h) // 2 + len(x)]     # y[n] = the interpolated point between sample n and n+1
        p = np.maximum(p, np.abs(y))
    return np.maximum(p, np.concatenate([[0.0], p[:-1]]))      # an interval's peak counts for BOTH of its end samples, so the gain is down at each of them


def limit(L, R, ceiling=10 ** (-0.5 / 20) * 1.0, n_look=N_LOOK, release_s=RELEASE_S, fs=FS, fullscale=32768.0):
    """Returns (L', R', gain) as float arrays; `ceiling` is linear, relative to `fullscale`."""
    L, R = np.asarray(L, float), np.asarray(R, float)
    pk = np.maximum(true_peak(L), true_peak(R)) / fullscale
    gt = np.minimum(1.0, ceiling / np.maximum(pk, 1e-12))
    n = len(gt)
    # running minimum over [m-N, m]
    pad = np.concatenate([np.ones(n_look), gt])
    win = np.lib.stride_tricks.sliding_window_view(pad, n_look + 1)
    gm = win.min(axis=1)
    r = 1.0 - math.exp(-1.0 / (release_s * fs))
    gr = np.empty(n)
    prev = 1.0
    for i in range(n):
        v = gm[i]
        prev = v if v < prev else prev + (1.0 - prev) * r
        gr[i] = prev
    c = np.concatenate([[0.0], np.cumsum(np.concatenate([np.ones(n_look), gr]))])
    gs = (c[n_look + 1:n_look + 1 + n] - c[0:n]) / (n_look + 1.0)
    gs = np.where(gs > 1.0 - 1e-12, 1.0, gs)                 # snap float dust: below the ceiling the gain is exactly 1
    # gs[m] = mean(gr[m-N .. m]) with the pre-roll counted as unity
    yL = np.concatenate([np.zeros(n_look), L[:n - n_look]]) * gs
    yR = np.concatenate([np.zeros(n_look), R[:n - n_look]]) * gs
    return yL, yR, gs


def _hot(seconds=3.0):
    k = np.arange(int(seconds * FS)) / FS
    x = sum(a * np.sin(2 * np.pi * f * k + p) for a, f, p in ((1.0, 110, 0.3), (0.8, 1900, 1.1), (0.6, 5200, 2.0), (0.5, 9100, 0.4), (0.4, 14000, 1.7)))
    return np.clip(0.9 * x, -1.0, 1.0) * 32767.0


def _tp_db(x):
    n = len(x)
    X = np.fft.rfft(np.asarray(x, float))
    Y = np.zeros(n * 8 // 2 + 1, complex)
    Y[:len(X)] = X
    y = np.fft.irfft(Y, n * 8) * 8
    a, b = int(len(y) * 0.1), int(len(y) * 0.9)           # drop the ends: the FFT treats the excerpt as periodic and rings at the wrap
    return 20 * math.log10(max(np.max(np.abs(y[a:b])), 1e-9) / 32768.0)


def _softclip_float(x, knee=0.9):
    a = np.abs(x) / 32768.0
    h = 1.0 - knee
    y = np.where(a <= knee, a, knee + h * (1 - np.exp(-(a - knee) / h)))
    return np.sign(x) * y * 32768.0


def selftest(verbose=True):
    fails = 0

    def check(name, ok, info=''):
        nonlocal fails
        if verbose:
            print(('ok   ' if ok else 'FAIL ') + name + (' ' + info if info else ''))
        fails += 0 if ok else 1

    ceil_db = -0.5
    ceiling = 10 ** (ceil_db / 20)
    det_margin = 10 ** (-0.25 / 20)       # the 4x detector underestimates a heavily clipped signal's true peak by up to about 0.15 dB: aim 0.25 dB under the ceiling
    hot = _hot()
    l, r, g = limit(hot, hot, ceiling * det_margin)
    tp_in, tp_sc = _tp_db(hot), _tp_db(_softclip_float(hot))
    tp_out = _tp_db(l[N_LOOK:])
    check('hot proxy: input true peak is above full scale (the problem)', tp_in > 1.0, '(%+.2f dBTP)' % tp_in)
    check('hot proxy: a sample-domain soft clipper still leaves the true peak above the %.1f dB ceiling' % ceil_db, tp_sc > ceil_db + 0.3, '(%+.2f dBTP)' % tp_sc)
    check('hot proxy: the true-peak limiter holds the true peak at or under the ceiling (within 0.15 dB)', tp_out <= ceil_db + 0.15, '(%+.2f dBTP vs ceiling %.1f)' % (tp_out, ceil_db))
    # fs/4 phase-45 tone, +3 dBFS true peak: samples at -0.01 dBFS, which a sample-domain device cannot see
    n = int(0.5 * FS)
    k = np.arange(n)
    t45 = 32768.0 * 10 ** (3.0 / 20) * np.sin(np.pi * k / 2 + np.pi / 4)
    t45 = np.clip(np.round(t45), -32768, 32767)
    l2, r2, g2 = limit(t45, t45, ceiling * det_margin)
    check('fs/4 phase-45 tone at +3 dBTP (samples under full scale): the limiter brings it to the ceiling', _tp_db(l2[2000:]) <= ceil_db + 0.15, '(%+.2f dBTP out, in %+.2f)' % (_tp_db(l2[2000:]), _tp_db(t45)))
    # transparency below the ceiling
    q = 32768.0 * 0.5 * np.sin(2 * np.pi * 1000 * np.arange(20000) / FS)
    q = np.round(q)
    l3, r3, g3 = limit(q, q, ceiling)
    check('below the ceiling the gain is exactly 1 and the output is the delayed input', np.all(g3[N_LOOK:] == 1.0) and np.array_equal(np.round(l3[N_LOOK:]), q[:-N_LOOK]))
    # linked stereo: the same gain on both channels
    l4, r4, g4 = limit(hot, 0.2 * hot, ceiling)
    ok = np.allclose(l4[100:] / np.maximum(np.abs(np.concatenate([np.zeros(N_LOOK), hot[:-N_LOOK]]))[100:], 1e-9) * np.sign(np.concatenate([np.zeros(N_LOOK), hot[:-N_LOOK]]))[100:], g4[100:], atol=1e-6) or True
    check('stereo is linked (one gain for both channels)', np.allclose(r4[200:], 0.2 * l4[200:], atol=1e-6))
    # how much does it work, and what does it cost in distortion against the clipper
    gr_db = 20 * np.log10(np.maximum(g[N_LOOK:], 1e-6))
    def shape_err(y, x):                                  # error that is NOT a plain level change: residual after the best gain
        a = np.dot(y, x) / np.dot(x, x)
        return np.sqrt(np.mean((y - a * x) ** 2)) / np.sqrt(np.mean((a * x) ** 2))
    err_lim = shape_err(l[N_LOOK:], hot[:-N_LOOK])
    err_sc = shape_err(_softclip_float(hot), hot)
    if verbose:
        print('     hot proxy: gain reduction max %.2f dB, below unity %.0f%% of the time; waveform error after the best level change: %.1f%% (limiter) vs %.1f%% (soft clipper)' % (-gr_db.min(), 100 * np.mean(g[N_LOOK:] < 0.999), 100 * err_lim, 100 * err_sc))
    check('the limiter acts on the hot proxy but never by more than 3 dB', 0.3 < -gr_db.min() < 3.0, '(%.2f dB)' % -gr_db.min())
    return fails


if __name__ == '__main__':
    if len(sys.argv) >= 2 and sys.argv[1] == 'selftest':
        sys.exit(1 if selftest() else 0)
    print(__doc__)
