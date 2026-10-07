#!/usr/bin/env python3
"""Headphone crossfeed host model (docs/features/CYMO_HEADPHONE_PLAN.md section 4, parallel plan A3, B-621). Our own derivation from the textbook interaural time and level difference
principle; no third-party crossfeed code was read (docs/PROVENANCE.md).

DIFFERENCE FORM (the candidate).  d = R - L ;  c = b * z^-D * LP(d) ;  L' = L + c ,  R' = R - c.
  Mono (L == R) gives d = 0, so a mono signal passes unchanged (bit-exactly in the integer version once the filter has settled). The cross path (the other ear: delayed D
  samples, one-pole low-passed at fc, scaled by b) is the usual crossfeed path; the direct path carries 1 - b z^-D LP, the price of mono invariance (a few dB of low-frequency
  loss and a little ripple, quantified below).
PLAIN FORM (the comparator).  L' = (L + k z^-D LP(R)) / (1 + k): the textbook crossfeed, normalised so that DC mono is unity (so mono treble is 1/(1+k) down).
MONO MODE.  L' = R' = (L + R) / 2 (the same datapath with the low-pass bypassed, delay 0, b = 1/2).

Defaults: fc 700 Hz (one-pole, bilinear), D = 4 samples at 48 kHz plus the low-pass's own 0.18 ms = about 0.26 ms cross-path delay at 200-500 Hz (the textbook interaural delay is 0.25-0.3 ms), b presets light 0.20 / medium 0.30 / strong 0.45. The integer version is the reference for a later RTL block:
24-bit signal path (16-bit input << 8), Q30 low-pass coefficients, Q15 gain, round to nearest.

    python3 tools/lab/cymo_crossfeed_model.py selftest
    python3 tools/lab/cymo_crossfeed_model.py render "track.flac" outdir/ [--seconds 20] [--skip 10]
"""
import math
import os
import sys
import wave

import numpy as np

FS = 48000.0
D = 4                 # extra delay in samples; the one-pole low-pass already adds about 0.18 ms of group delay, so D = 4 gives a cross-path delay of about 0.26 ms (B-621: D = 13 would be 0.45 ms)
FC = 700.0
PRESETS = {'light': 0.20, 'medium': 0.30, 'strong': 0.45}


def _lp_coefs(fc=FC, fs=FS):
    k = math.tan(math.pi * fc / fs)
    return k / (1 + k), (k - 1) / (k + 1)          # y[n] = g (x[n] + x[n-1]) - a y[n-1]


def _lp(x, fc=FC, fs=FS):
    g, a = _lp_coefs(fc, fs)
    if len(x) > 20000:
        # long signals (the listening render): the same filter by FFT convolution with its impulse response, truncated where it is below 1e-12 (the pole is at about -0.91)
        n = int(math.ceil(math.log(1e-12) / math.log(abs(a)))) + 2
        h = np.zeros(n)
        x1 = y1 = 0.0
        for i in range(n):
            v = 1.0 if i == 0 else 0.0
            y1 = g * (v + x1) - a * y1
            x1 = v
            h[i] = y1
        m = 1 << int(math.ceil(math.log2(len(x) + n)))
        return np.fft.irfft(np.fft.rfft(np.asarray(x, float), m) * np.fft.rfft(h, m), m)[:len(x)]
    y = np.zeros(len(x))
    x1 = y1 = 0.0
    for i, v in enumerate(x):
        y1 = g * (v + x1) - a * y1
        x1 = v
        y[i] = y1
    return y


def _delay(x, n):
    return np.concatenate([np.zeros(n), x[:len(x) - n]]) if n else np.asarray(x, float)


def crossfeed_diff(L, R, b=0.30, fc=FC, d=D, fs=FS):
    L, R = np.asarray(L, float), np.asarray(R, float)
    c = b * _delay(_lp(R - L, fc, fs), d)
    return L + c, R - c


def crossfeed_plain(L, R, k=0.30, fc=FC, d=D, fs=FS):
    L, R = np.asarray(L, float), np.asarray(R, float)
    return (L + k * _delay(_lp(R, fc, fs), d)) / (1 + k), (R + k * _delay(_lp(L, fc, fs), d)) / (1 + k)


def mono(L, R):
    m = (np.asarray(L, float) + np.asarray(R, float)) / 2.0
    return m, m.copy()


# ----------------------------------------------------------------------------- integer reference (what an RTL block would do)

def crossfeed_int(L, R, b=0.30, fc=FC, d=D, fs=FS, frac=8):
    """Integer reference. L, R are 16-bit integers. d_in = (R - L) << frac (24-bit), one-pole low-pass in Q30 with a 24-bit state, a d-sample delay line, gain in Q15,
    output L + round(c >> frac). Returns (L', R') as Python ints (not clamped; the output stage clamps)."""
    g, a = _lp_coefs(fc, fs)
    gq, aq = int(round(g * (1 << 30))), int(round(a * (1 << 30)))
    bq = int(round(b * (1 << 15)))
    line = [0] * d
    x1 = y1 = 0
    outl, outr = [], []
    for l, r in zip(L, R):
        x = (int(r) - int(l)) << frac
        y = (gq * (x + x1) - aq * y1 + (1 << 29)) >> 30
        x1, y1 = x, y
        line.append(y)
        yd = line.pop(0) if d else y
        c = (bq * yd + (1 << (14 + frac))) >> (15 + frac)
        outl.append(int(l) + c)
        outr.append(int(r) - c)
    return outl, outr


# ----------------------------------------------------------------------------- self-test

def _impulse_response(fn, n=4096):
    e = np.zeros(n)
    e[0] = 1.0
    z = np.zeros(n)
    a, b2 = fn(e, z)             # L-only impulse: a = direct path, b2 = cross path (to the other ear)
    return a, b2


def _group_delay_ms(h, f0, f1, fs=FS):
    f = np.linspace(f0, f1, 60)
    n = np.arange(len(h))
    ph = np.unwrap([np.angle(np.sum(h * np.exp(-2j * math.pi * ff * n / fs))) for ff in f])
    return float(-np.mean(np.gradient(ph, 2 * math.pi * f)) * 1000.0)


def _mag_db(h, f, fs=FS):
    n = np.arange(len(h))
    return 20 * math.log10(max(abs(np.sum(h * np.exp(-2j * math.pi * f * n / fs))), 1e-12))


def selftest(verbose=True):
    fails = 0

    def check(name, ok, info=''):
        nonlocal fails
        if verbose:
            print(('ok   ' if ok else 'FAIL ') + name + (' ' + info if info else ''))
        fails += 0 if ok else 1

    rng = np.random.default_rng(2)
    x = rng.normal(0, 3000, 8000)
    l2, r2 = crossfeed_diff(x, x)
    check('difference form: a mono signal passes unchanged (float)', np.max(np.abs(l2 - x)) < 1e-9 and np.max(np.abs(r2 - x)) < 1e-9)
    for name, b in PRESETS.items():
        dr, cr = _impulse_response(lambda l, r: crossfeed_diff(l, r, b))
        gd = _group_delay_ms(cr, 200, 500)
        check('%s (b %.2f): cross-path group delay at 200-500 Hz is 0.22-0.30 ms (the interaural delay)' % (name, b), 0.22 <= gd <= 0.30, '(%.3f ms)' % gd)
    dr, cr = _impulse_response(lambda l, r: crossfeed_diff(l, r, 0.30))
    lo_dir, lo_cross = _mag_db(dr, 100.0), _mag_db(cr, 100.0)
    hi_cross = _mag_db(cr, 8000.0)
    ripple = [_mag_db(dr, f) for f in (300, 500, 700, 1000, 1500, 2000, 4000, 8000)]
    check('b 0.30: cross path about -10.5 dB at 100 Hz and under -30 dB at 8 kHz', -11.5 < lo_cross < -9.5 and hi_cross < -30.0, '(%.1f / %.1f dB)' % (lo_cross, hi_cross))
    check('b 0.30: direct path about -3.0 dB at 100 Hz, between -2.4 and +0.7 dB from 300 Hz up', -3.4 < lo_dir < -2.6 and min(ripple) > -2.4 and max(ripple) < 0.7, '(%.1f dB at 100 Hz, range %.2f..%.2f dB)' % (lo_dir, min(ripple), max(ripple)))
    pl, pc = _impulse_response(lambda l, r: crossfeed_plain(l, r, 0.30))
    xs = rng.normal(0, 3000, 8000)
    mp, _ = crossfeed_plain(xs, xs)
    # plain form: mono treble is down by 1/(1+k): the loudness/tone change the difference form avoids
    hf = 20 * math.log10(np.std(mp[200:]) / np.std(xs[200:]))
    check('plain form (k 0.30) darkens a mono signal by about 2.3 dB (what the difference form avoids)', -2.9 < hf < -1.7, '(%.2f dB)' % hf)
    ml, mr = mono(x, x * 0.5)
    check('mono mode: both outputs are (L + R)/2', np.allclose(ml, 0.75 * x) and np.allclose(mr, ml))
    # worst-case overshoot: a full-scale anti-phase sine, swept over frequency (time domain, steady state)
    t = np.arange(24000)
    worst, wf = 0.0, 0.0
    for f in (500, 800, 1200, 1800, 2500, 3500, 5000, 8000):
        s = 32767 * np.sin(2 * np.pi * f * t / FS)
        lo, ro = crossfeed_diff(s, -s, 0.30)
        g = 20 * math.log10(np.max(np.abs(lo[4000:])) / 32767)
        if g > worst:
            worst, wf = g, f
    check('worst case (full-scale anti-phase sine, b 0.30): about +1 dB over full scale, not more', 0.6 < worst < 1.5, '(%.2f dB at %d Hz)' % (worst, wf))
    # integer reference
    xi = np.round(rng.normal(0, 6000, 6000)).astype(int)
    yi = np.round(rng.normal(0, 6000, 6000)).astype(int)
    fl, fr = crossfeed_diff(xi, yi, 0.30)
    il, ir = crossfeed_int(xi.tolist(), yi.tolist(), 0.30)
    err = max(np.max(np.abs(np.array(il) - fl)), np.max(np.abs(np.array(ir) - fr)))
    check('integer reference is within 1 LSB of the float model', err < 1.0, '(worst %.3f LSB)' % err)
    # stereo, then mono: the integer version settles to exact pass-through
    st = np.round(rng.normal(0, 8000, 3000)).astype(int)
    st2 = np.round(rng.normal(0, 8000, 3000)).astype(int)
    mo = np.round(rng.normal(0, 8000, 4000)).astype(int)
    il, ir = crossfeed_int(np.concatenate([st, mo]).tolist(), np.concatenate([st2, mo]).tolist(), 0.45)
    tail = slice(3000 + 2500, 7000)
    exact = np.array_equal(np.array(il)[tail], mo[2500:]) and np.array_equal(np.array(ir)[tail], mo[2500:])
    check('integer version: after stereo then mono the output equals the input exactly once the filter has settled', exact)
    il, ir = crossfeed_int([0] * 500, [0] * 500, 0.30)
    check('integer version: silence in, silence out (no limit cycle)', all(v == 0 for v in il + ir))
    return fails


# ----------------------------------------------------------------------------- listening render

def _write_wav(path, rate, L, R, gain):
    pcm = np.empty(len(L) * 2, '<i2')
    pcm[0::2] = np.clip(np.round(np.asarray(L) * gain), -32768, 32767)
    pcm[1::2] = np.clip(np.round(np.asarray(R) * gain), -32768, 32767)
    with wave.open(path, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())


def _decode(track, seconds, skip):
    """16-bit stereo excerpt. macOS afconvert decodes a lossless file exactly and fast (the repo's pure-Python bit-exact decoder, tools/lab/cymo_ab_listen.decode_pcm, runs at
    about 1/13 of real time); it is used when present, otherwise the repo decoder."""
    import shutil, subprocess, tempfile
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    if shutil.which('afconvert'):
        import cymo_loopback as c
        with tempfile.TemporaryDirectory() as d:
            wav = os.path.join(d, 'x.wav')
            subprocess.run(['afconvert', '-f', 'WAVE', '-d', 'LEI16', track, wav], check=True, capture_output=True)
            rate, l = c.read_wav(wav, 0)
            _, r = c.read_wav(wav, 1)
        a, b = int(skip * rate), int((skip + seconds) * rate)
        return np.asarray(l[a:b], float) * 32768.0, np.asarray(r[a:b], float) * 32768.0, rate
    import cymo_ab_listen as ab
    rate, bps, L, R = ab.decode_pcm(track, max_seconds=seconds, skip_seconds=skip)
    L, R = np.asarray(L, float), np.asarray(R, float)
    if bps > 16:
        L, R = L / 2 ** (bps - 16), R / 2 ** (bps - 16)
    return L, R, rate


def render(track, outdir, seconds=20.0, skip=10.0):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    L, R, rate = _decode(track, seconds, skip)
    os.makedirs(outdir, exist_ok=True)
    name = os.path.splitext(os.path.basename(track))[0].replace(' ', '_')[:28]
    variants = [('A_original', (L, R)), ('B_diff_light_0.20', crossfeed_diff(L, R, 0.20)), ('C_diff_medium_0.30', crossfeed_diff(L, R, 0.30)),
                ('D_diff_strong_0.45', crossfeed_diff(L, R, 0.45)), ('E_plain_0.30', crossfeed_plain(L, R, 0.30)), ('F_mono', mono(L, R))]
    peak = max(float(np.max(np.abs(np.concatenate(v)))) for _, v in variants)
    gain = min(1.0, 30000.0 / peak)                    # one common gain for every file: levels stay comparable and nothing clips
    for tag, (l, r) in variants:
        path = os.path.join(outdir, '%s_%s.wav' % (name, tag))
        _write_wav(path, rate, l, r, gain)
        side = np.std(l - r) / max(np.std(l + r), 1e-9)
        print('%-52s side/mid %.3f  rms %.1f dBFS' % (os.path.basename(path), side, 20 * math.log10(max(np.std(np.concatenate([l, r])) * gain / 32768, 1e-9))))
    print('common gain %.3f; sample peak before gain %.0f' % (gain, peak))


if __name__ == '__main__':
    if len(sys.argv) >= 2 and sys.argv[1] == 'selftest':
        sys.exit(1 if selftest() else 0)
    if len(sys.argv) >= 4 and sys.argv[1] == 'render':
        sec = float(sys.argv[sys.argv.index('--seconds') + 1]) if '--seconds' in sys.argv else 20.0
        skip = float(sys.argv[sys.argv.index('--skip') + 1]) if '--skip' in sys.argv else 10.0
        render(sys.argv[2], sys.argv[3], sec, skip)
    else:
        print(__doc__)
