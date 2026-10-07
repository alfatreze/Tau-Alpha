#!/usr/bin/env python3
"""Inter-sample-peak (ISP) test files and analyser for the Pocket's headphone output (gate G-ISP, docs/features/CYMO_RECORDING_PLAN_DEV105.md, B-617).

QUESTION. A digital signal whose samples stay inside +-1.0 can still reconstruct to a waveform above 1.0 BETWEEN the samples (the true peak). A DAC's
oversampling interpolator, or the amp after it, may clip those peaks. This tool makes files with a KNOWN true peak, and says from a recording whether the
Pocket's output chain compresses or distorts above 0 dBFS true peak. It decides the final soft-clipper ceiling and whether a true-peak limiter is needed.

FILES (48 kHz, 16-bit, so the resampler is not involved; play them with CYMO RESAMPLER off, EQ FLAT, ReplayGain off, a volume where the whole chain is linear,
and record them with the headphone output as in the recording plan; never change the interface gain between the three files):
  isp_ladder_48000.flac  12 kHz (fs/4) sine sections, 4 s each, 1 s apart after 2 s of silence. Phase 0: samples (0, a, 0, -a), sample peak = true peak, at
                         -12, -6, -3, 0 dBFS. Phase 45 deg: samples (+s, +s, -s, -s), s = 0.707 a: the sample peak is 3 dB below the true peak, so a true peak
                         of -12, -6, -3, 0, +1, +2, +3 dBFS fits in samples that never exceed -0.01 dBFS.
  isp_imd_48000.flac     1 kHz carrier at -20 dBFS plus the 12 kHz phase-45 tone at a true peak of -12, -6, 0, +1, +2 dBFS (samples stay under 0.991).
                         A clipped peak makes intermodulation products at 11 and 13 kHz (and 10, 14 kHz) that a linear chain does not.
  isp_hot_48000.flac     a hard-clipped multi-partial 'hot master' proxy (sample peak 0 dBFS, true peak about +2 dB), then the same signal 6 dB lower.
`analyze` reads a recording of one file and gives a verdict per section. `selftest` checks the analyser against simulated linear and clipping chains.

    python3 tools/lab/cymo_isp.py gen  ~/cymo_loopback
    python3 tools/lab/cymo_isp.py analyze rec_isp_ladder.wav --kind ladder
    python3 tools/lab/cymo_isp.py analyze rec_isp_imd.wav --kind imd
    python3 tools/lab/cymo_isp.py analyze rec_isp_hot.wav --kind hot
"""
import argparse
import json
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cymo_loopback as c

RATE = 48000
LEAD_S, SEC_S, GAP_S = 2.0, 4.0, 1.0
F12 = 12000.0
LADDER_P0 = (-12.0, -6.0, -3.0, 0.0)
LADDER_P45 = (-12.0, -6.0, -3.0, 0.0, 1.0, 2.0, 3.0)
IMD_T = (-12.0, -6.0, 0.0, 1.0, 2.0)
IMD_CARRIER_DB = -20.0
HOT_PARTIALS = ((1.0, 110, 0.3), (0.8, 1900, 1.1), (0.6, 5200, 2.0), (0.5, 9100, 0.4), (0.4, 14000, 1.7))
HOT_SECONDS = 12.0
COMPRESSION_DB = 0.3          # a ladder section whose gain is this far under the low-level sections is compressed
IMD_RISE_DB = 6.0             # IMD more than this above the lowest-level section (and above -70 dBc) is flagged


def _n(sec):
    return int(round(sec * RATE))


def _fs4(a, phase_deg, n):
    k = np.arange(n)
    return a * np.sin(2 * np.pi * k / 4.0 + math.radians(phase_deg))


def _q(x):
    """To 16-bit integers; the sample peak must not clip."""
    v = np.rint(np.asarray(x) * 32767.0)
    assert np.max(np.abs(v)) <= 32767, 'sample peak would clip'
    return v.astype(np.int64)


def true_peak_db(x, os_=8):
    """Peak of the reconstructed (band-limited) waveform, by FFT oversampling."""
    x = np.asarray(x, float)
    n = len(x)
    X = np.fft.rfft(x)
    Y = np.zeros(n * os_ // 2 + 1, complex)
    Y[:len(X)] = X
    y = np.fft.irfft(Y, n * os_) * os_
    return 20 * math.log10(max(np.max(np.abs(y)), 1e-12))


def _assemble(sections):
    """sections: list of (float array, meta dict). Returns (float stream, meta list with start/stop sample)."""
    parts, meta, pos = [np.zeros(_n(LEAD_S))], [], _n(LEAD_S)
    for x, m in sections:
        m = dict(m, start=pos, stop=pos + len(x))
        meta.append(m)
        parts.append(x)
        parts.append(np.zeros(_n(GAP_S)))
        pos += len(x) + _n(GAP_S)
    return np.concatenate(parts), meta


def ladder():
    n = _n(SEC_S)
    secs = []
    for t in LADDER_P0:
        secs.append((_fs4(10 ** (t / 20), 0, n), dict(phase=0, true_db=t)))
    for t in LADDER_P45:
        secs.append((_fs4(10 ** (t / 20), 45, n), dict(phase=45, true_db=t)))
    return _assemble([(_q(x) / 32767.0, m) for x, m in secs])


def imd():
    n = _n(SEC_S)
    k = np.arange(n)
    carrier = 10 ** (IMD_CARRIER_DB / 20) * np.sin(2 * np.pi * 1000.0 * k / RATE)
    secs = []
    for t in IMD_T:
        secs.append((carrier + _fs4(10 ** (t / 20), 45, n), dict(phase=45, true_db=t)))
    return _assemble([(_q(x) / 32767.0, m) for x, m in secs])


def hot():
    n = _n(HOT_SECONDS)
    k = np.arange(n) / RATE
    x = sum(a * np.sin(2 * np.pi * f * k + p) for a, f, p in HOT_PARTIALS)
    h0 = np.clip(0.9 * x, -1.0, 1.0)
    return _assemble([(_q(h0) / 32767.0, dict(level_db=0.0, name='hot')), (_q(h0 * 0.5) / 32767.0, dict(level_db=-6.0, name='hot_m6'))])


KINDS = {'ladder': ladder, 'imd': imd, 'hot': hot}


def gen(outdir):
    os.makedirs(outdir, exist_ok=True)
    manifest = {}
    print('%-24s %-8s %s' % ('file', 'size', 'sections (true peak dBFS, sample peak dBFS)'))
    for kind, fn in KINDS.items():
        x, meta = fn()
        for m in meta:
            seg = x[m['start']:m['stop']]
            m['sample_peak_db'] = round(20 * math.log10(max(np.max(np.abs(seg)), 1e-12)), 2)
            m['true_peak_db_measured'] = round(true_peak_db(seg[:1 << 16]), 2)
        name = 'isp_%s_48000.flac' % kind
        pcm = [(int(v), int(v)) for v in _q(x)]
        path = os.path.join(outdir, name)
        c._write_flac(path, pcm, RATE)
        manifest[name] = meta
        print('%-24s %5.1f MB  %s' % (name, os.path.getsize(path) / 1e6, '; '.join('%+.1f/%+.2f' % (m.get('true_db', m.get('level_db')), m['sample_peak_db']) if kind != 'hot' else '%s peak %.2f true %.2f' % (m['name'], m['sample_peak_db'], m['true_peak_db_measured']) for m in meta)))
    with open(os.path.join(outdir, 'isp_manifest.json'), 'w') as f:
        json.dump(manifest, f, indent=1)
    print('\nManifest: isp_manifest.json. Copy the three FLAC files into a core\'s Assets/<core>/common/ folder (card write: ask first) and rescan the library.')


# ----------------------------------------------------------------------------- analysis

def find_sections(x, rate=RATE, min_s=1.0, expect=None):
    """Tone/signal sections of a recording: 50 ms RMS above a threshold 25 dB over the quiet floor, short gaps merged, short blips dropped."""
    blk = int(0.05 * rate)
    nb = len(x) // blk
    rms = np.sqrt(np.mean(np.reshape(x[:nb * blk], (nb, blk)) ** 2, axis=1)) + 1e-12
    floor = np.percentile(rms, 10)
    on = rms > floor * 10 ** (25 / 20)
    secs, i = [], 0
    while i < nb:
        if on[i]:
            j = i
            while j < nb and on[j]:
                j += 1
            secs.append([i, j])
            i = j
        else:
            i += 1
    merged = []
    for s in secs:
        if merged and (s[0] - merged[-1][1]) * 0.05 < 0.3:
            merged[-1][1] = s[1]
        else:
            merged.append(s)
    out = [(a * blk, b * blk) for a, b in merged if (b - a) * 0.05 >= min_s]
    return out


def _mid(x, a, b, frac=0.6, nmax=1 << 17):
    n = b - a
    lo = a + int(n * (1 - frac) / 2)
    seg = x[lo:lo + int(n * frac)]
    return seg[:nmax]


def fit_tone(seg, f_nominal, rate=RATE):
    """Peak amplitude (linear) and refined frequency of the tone near f_nominal: parabolic FFT peak, then a least-squares sin/cos fit."""
    n = len(seg)
    w = np.hanning(n)
    X = np.abs(np.fft.rfft((seg - seg.mean()) * w, n * 4))
    hz = rate / (n * 4.0)
    c0 = int(round(f_nominal / hz))
    lo, hi = c0 - int(60 / hz), c0 + int(60 / hz)
    k = lo + int(np.argmax(X[lo:hi]))
    a, b, g = np.log(X[k - 1] + 1e-30), np.log(X[k] + 1e-30), np.log(X[k + 1] + 1e-30)
    f = (k + 0.5 * (a - g) / (a - 2 * b + g)) * hz
    t = np.arange(n) / rate
    A = np.c_[np.sin(2 * np.pi * f * t), np.cos(2 * np.pi * f * t), np.ones(n)]
    co = np.linalg.lstsq(A, seg, rcond=None)[0]
    return float(np.hypot(co[0], co[1])), float(f)


def _band_db(seg, f, rate=RATE, width=60.0):
    """Power in dB (arbitrary reference, consistent between calls) in +-width Hz around f, Hann-windowed."""
    n = len(seg)
    w = np.hanning(n)
    P = np.abs(np.fft.rfft((seg - seg.mean()) * w)) ** 2
    hz = rate / n
    lo, hi = int((f - width) / hz), int((f + width) / hz) + 1
    return 10 * math.log10(max(P[lo:hi].sum(), 1e-30)), 10 * math.log10(max(P.sum(), 1e-30))


def analyze_ladder(x, rate=RATE):
    secs = find_sections(x, rate)
    expected = [(0, t) for t in LADDER_P0] + [(45, t) for t in LADDER_P45]
    if len(secs) != len(expected):
        return {'error': 'found %d sections, expected %d: check that the whole file was recorded and the levels are not clipped' % (len(secs), len(expected))}
    rows = []
    for (a, b), (ph, t) in zip(secs, expected):
        amp, f = fit_tone(_mid(x, a, b), F12, rate)
        lvl = 20 * math.log10(max(amp, 1e-12))
        rows.append(dict(phase=ph, true_db=t, level_dbfs=round(lvl, 2), freq=round(f, 1), gain=lvl - t))
    ref = np.mean([r['gain'] for r in rows if r['true_db'] <= -6.0])
    for r in rows:
        r['dev_db'] = round(r['gain'] - ref, 2)
        r['verdict'] = 'COMPRESSED' if r['dev_db'] < -COMPRESSION_DB else 'ok'
        r['gain'] = round(r['gain'], 2)
    over = [r for r in rows if r['true_db'] > 0.0]
    summary = 'clean up to +%.0f dBFS true peak' % max([r['true_db'] for r in rows if r['verdict'] == 'ok'] + [0])
    bad = [r for r in rows if r['verdict'] != 'ok']
    if bad:
        first = min(bad, key=lambda r: r['true_db'])
        summary = 'COMPRESSION from a true peak of %+.0f dBFS (%+.2f dB at phase %d)' % (first['true_db'], first['dev_db'], first['phase'])
    return {'rows': rows, 'reference_gain_db': round(float(ref), 2), 'summary': summary, 'over_0dbfs_sections': len(over)}


def analyze_imd(x, rate=RATE):
    secs = find_sections(x, rate)
    if len(secs) != len(IMD_T):
        return {'error': 'found %d sections, expected %d' % (len(secs), len(IMD_T))}
    rows = []
    for (a, b), t in zip(secs, IMD_T):
        seg = _mid(x, a, b)
        amp, f = fit_tone(seg, F12, rate)
        tone_db, _ = _band_db(seg, f, rate)
        prod = 10 * math.log10(sum(10 ** (_band_db(seg, f + d, rate)[0] / 10) for d in (-2000.0, -1000.0, 1000.0, 2000.0)))
        rows.append(dict(true_db=t, level_dbfs=round(20 * math.log10(max(amp, 1e-12)), 2), imd_dbc=round(prod - tone_db, 2)))
    base = rows[0]['imd_dbc']
    for r in rows:
        r['rise_db'] = round(r['imd_dbc'] - base, 2)
        r['verdict'] = 'IMD' if (r['rise_db'] > IMD_RISE_DB and r['imd_dbc'] > -70.0) else 'ok'
    bad = [r for r in rows if r['verdict'] != 'ok']
    summary = 'no intermodulation rise up to a true peak of %+.0f dBFS' % rows[-1]['true_db']
    if bad:
        first = min(bad, key=lambda r: r['true_db'])
        summary = 'INTERMODULATION appears from a 12 kHz true peak of %+.0f dBFS (%.1f dBc, +%.1f dB)' % (first['true_db'], first['imd_dbc'], first['rise_db'])
    return {'rows': rows, 'summary': summary}


def _welch(seg, nfft=1 << 15, rate=RATE):
    w = np.hanning(nfft)
    acc, cnt = np.zeros(nfft // 2 + 1), 0
    for s in range(0, len(seg) - nfft + 1, nfft // 2):
        acc += np.abs(np.fft.rfft(seg[s:s + nfft] * w)) ** 2
        cnt += 1
    return acc / max(cnt, 1), np.fft.rfftfreq(nfft, 1 / rate)


def analyze_hot(x, rate=RATE):
    secs = find_sections(x, rate, min_s=5.0)
    if len(secs) != 2:
        return {'error': 'found %d sections, expected 2' % len(secs)}
    pa, f = _welch(_mid(x, *secs[0], frac=0.8, nmax=1 << 20))
    pb, _ = _welch(_mid(x, *secs[1], frac=0.8, nmax=1 << 20))
    band = (f > 20) & (f < 18000)
    floor_a = np.median(pa[band])
    lines = band & (pa > floor_a * 10 ** 2.5) & (pb > np.median(pb[band]) * 10 ** 2.5)
    ratio = 10 * np.log10(pa[lines] / pb[lines])
    dev = ratio - np.median(ratio)
    p95, mx = float(np.percentile(np.abs(dev), 95)), float(np.max(np.abs(dev)))
    verdict = 'NONLINEAR above 0 dBFS true peak' if (mx > 1.0 and p95 > 0.5) else 'linear'
    return {'lines': int(lines.sum()), 'gain_step_db': round(float(np.median(ratio)), 2), 'p95_dev_db': round(p95, 2), 'max_dev_db': round(mx, 2),
            'summary': '%s (%d spectral lines, gain step %.2f dB, deviation p95 %.2f dB, max %.2f dB)' % (verdict, int(lines.sum()), float(np.median(ratio)), p95, mx)}


def print_result(kind, r):
    if 'error' in r:
        print('ERROR: ' + r['error'])
        return
    if kind == 'ladder':
        print('%-6s %-10s %-12s %-9s %-8s %s' % ('phase', 'true peak', 'level dBFS', 'gain dB', 'dev dB', 'verdict'))
        for q in r['rows']:
            print('%-6d %+7.1f    %-12.2f %-9.2f %-8.2f %s' % (q['phase'], q['true_db'], q['level_dbfs'], q['gain'], q['dev_db'], q['verdict']))
    if kind == 'imd':
        print('%-10s %-12s %-9s %-9s %s' % ('true peak', 'level dBFS', 'IMD dBc', 'rise dB', 'verdict'))
        for q in r['rows']:
            print('%+7.1f    %-12.2f %-9.2f %-9.2f %s' % (q['true_db'], q['level_dbfs'], q['imd_dbc'], q['rise_db'], q['verdict']))
    print('=> ' + r['summary'])


# ----------------------------------------------------------------------------- selftest

def _recording(kind, chain, gain=0.25, noise_db=-76.0, seed=1):
    """A simulated capture of a file through `chain` (None = linear): the stream, a gain and interface/amp noise."""
    x, meta = KINDS[kind]()
    y = np.array(x)
    if chain is not None:
        for m in meta:
            a, b = m['start'] - 4096, m['stop'] + 4096
            y[a:b] = chain(y[a:b])
    rng = np.random.default_rng(seed)
    return y * gain + rng.normal(0, 10 ** (noise_db / 20), len(y))


def _clip_chain(ceiling, os_=8):
    """A DAC whose interpolation clips the reconstructed waveform at `ceiling` (full scale = 1.0)."""
    def chain(seg):
        n = len(seg)
        X = np.fft.rfft(seg)
        Y = np.zeros(n * os_ // 2 + 1, complex)
        Y[:len(X)] = X
        y = np.clip(np.fft.irfft(Y, n * os_) * os_, -ceiling, ceiling)
        return np.fft.irfft(np.fft.rfft(y)[:n // 2 + 1], n) / os_ * 1.0
    return chain


def selftest(verbose=True):
    fails = 0

    def check(name, ok, info=''):
        nonlocal fails
        if verbose:
            print(('ok   ' if ok else 'FAIL ') + name + (' ' + info if info else ''))
        fails += 0 if ok else 1

    # the generated files: sample peak, true peak, count
    x, meta = ladder()
    tp = {(m['phase'], m['true_db']): true_peak_db(x[m['start']:m['stop']][:1 << 16]) for m in meta}
    check('ladder: phase-45 sections reach their stated true peak (to 0.05 dB) with samples under -0.01 dBFS',
          all(abs(tp[(45, t)] - t) < 0.05 for t in LADDER_P45) and max(np.max(np.abs(x[m['start']:m['stop']])) for m in meta if m['phase'] == 45) < 0.9999)
    check('ladder: phase-0 sections have sample peak equal to the true peak', all(abs(tp[(0, t)] - t) < 0.05 for t in LADDER_P0))
    h, hm = hot()
    check('hot: sample peak 0 dBFS, true peak above +1.5 dB, second section 6.02 dB lower',
          abs(np.max(np.abs(h[hm[0]['start']:hm[0]['stop']])) - 1.0) < 1e-3 and true_peak_db(h[hm[0]['start']:hm[0]['start'] + (1 << 17)]) > 1.5)
    # linear chain: everything clean
    r = analyze_ladder(_recording('ladder', None))
    check('linear chain: ladder verdicts all ok', 'rows' in r and all(q['verdict'] == 'ok' for q in r['rows']), r.get('summary', r.get('error', '')))
    r = analyze_imd(_recording('imd', None))
    check('linear chain: no IMD flagged', 'rows' in r and all(q['verdict'] == 'ok' for q in r['rows']), r.get('summary', r.get('error', '')))
    r = analyze_hot(_recording('hot', None))
    check('linear chain: hot signal reads linear', r.get('summary', '').startswith('linear'), r.get('summary', r.get('error', '')))
    # a chain that clips the reconstructed waveform at full scale (what a converter with no inter-sample headroom does)
    r = analyze_ladder(_recording('ladder', _clip_chain(1.0)))
    ok = 'rows' in r and all(q['verdict'] == 'ok' for q in r['rows'] if q['true_db'] <= 0.0) and all(q['verdict'] == 'COMPRESSED' for q in r['rows'] if q['true_db'] >= 2.0 and q['phase'] == 45)
    check('clip at 1.0: compression found only above 0 dBFS true peak, and at +2 and +3', ok, r.get('summary', r.get('error', '')))
    r = analyze_imd(_recording('imd', _clip_chain(1.0)))
    check('clip at 1.0: intermodulation appears at the high sections', 'rows' in r and any(q['verdict'] == 'IMD' for q in r['rows']) and r['rows'][0]['verdict'] == 'ok', r.get('summary', r.get('error', '')))
    r = analyze_hot(_recording('hot', _clip_chain(1.0)))
    check('clip at 1.0: hot signal reads nonlinear', r.get('summary', '').startswith('NONLINEAR'), r.get('summary', r.get('error', '')))
    # a chain with headroom (clips at +3.5 dB) is clean for everything we send
    r = analyze_ladder(_recording('ladder', _clip_chain(10 ** (3.5 / 20))))
    check('clip at +3.5 dB: ladder clean (the files cannot exceed +3 dB)', 'rows' in r and all(q['verdict'] == 'ok' for q in r['rows']), r.get('summary', r.get('error', '')))
    return fails


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    g = sub.add_parser('gen', help='write the three ISP FLAC files and a manifest')
    g.add_argument('outdir')
    a = sub.add_parser('analyze', help='analyse a recording of one file')
    a.add_argument('wav')
    a.add_argument('--kind', choices=sorted(KINDS), required=True)
    a.add_argument('--channel', type=int, default=0)
    sub.add_parser('selftest')
    args = ap.parse_args()
    if args.cmd == 'gen':
        gen(args.outdir)
    elif args.cmd == 'analyze':
        rate, x = c.read_wav(args.wav, args.channel)
        x = np.asarray(x, float)
        r = {'ladder': analyze_ladder, 'imd': analyze_imd, 'hot': analyze_hot}[args.kind](x, rate)
        print(os.path.basename(args.wav))
        print_result(args.kind, r)
    else:
        sys.exit(1 if selftest() else 0)


if __name__ == '__main__':
    main()
