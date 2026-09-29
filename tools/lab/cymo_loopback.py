#!/usr/bin/env python3
"""Cymo analog loopback measurement: test files and analyser (docs/features/CYMO_AUDIO_ENGINE.md section 6.7).

Purpose: show what the Pocket's real analog output does, so every later Cymo change can be
compared against a recorded baseline instead of a model. Standard library only (uses numpy for
speed when it happens to be installed).

Three steps:

  1. Make the test files (lossless FLAC, so the decoder is not a variable) and copy them into a
     core's Assets/<core>/common/ folder on the SD card:

         python3 tools/lab/cymo_loopback.py gen ~/cymo_loopback

  2. Play each file on the Pocket. Headphone jack -> 3.5 mm cable -> USB audio interface line-in.
     Record each one on the computer as WAV (Audacity is fine), 48 kHz, 24-bit if offered.
     Keep the interface input gain, the Pocket volume and the EQ (FLAT) the same for every capture,
     and name the recordings after the test file. Record a little silence before and after.

  3. Analyse:

         python3 tools/lab/cymo_loopback.py analyze rec_tone_1k_44100.wav --freq 1000 --json base_1k_44100.json
         python3 tools/lab/cymo_loopback.py analyze rec_sweep.wav --sweep
         python3 tools/lab/cymo_loopback.py compare base_1k_44100.json later_1k_44100.json

`selftest` checks the analyser against known signals, including the nearest-neighbour
resampling error the plan predicts for 44.1 kHz material on a 48 kHz DAC.

What the numbers mean (per tone):
  level      peak level of the tone in dBFS of the recording
  sinad      tone power over everything else (noise, distortion, images), in dB; higher is better
  thd        power of harmonics 2..5 relative to the tone, in dB; lower is better
  spurs      the strongest non-harmonic components and their level relative to the tone (dBc);
             a resampler that only repeats samples shows up here as image tones
  floor      median level of the remaining bins, in dBFS
"""
import argparse
import cmath
import json
import math
import os
import struct
import sys

try:                                   # optional speed-up only
    import numpy as _np
except Exception:                      # pragma: no cover
    _np = None

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

# ----------------------------------------------------------------------------- test files

SECONDS_TONE = 60
LEVEL_STEPS = (-12.0, -6.0, -3.0, -0.1)      # dBFS, 3 s each, 1 kHz


def _to_pcm(x, amp=32767):
    v = int(round(x * amp))
    return max(-32768, min(32767, v))


def _pad_block(n, block):
    return block * int(math.ceil(n / float(block)))


def _write_flac(path, samples, rate):
    """16-bit stereo VERBATIM FLAC, via the repo's own generator (tools/flac_make_test.py).
    `samples` is a list of (l, r) integer pairs; it is padded with silence to whole blocks."""
    import flac_make_test as fm
    block = 4096
    total = _pad_block(len(samples), block)
    samples = samples + [(0, 0)] * (total - len(samples))
    frames = b''
    for i in range(total // block):
        frames += fm.make_frame(i, samples[i * block:(i + 1) * block], 16, rate, block, 1)
    si = fm.BitWriter()
    si.write(block, 16)
    si.write(block, 16)
    si.write(0, 24)
    si.write(0, 24)
    si.write(rate, 20)
    si.write(1, 3)
    si.write(15, 5)
    si.write(total, 36)
    for b in fm.pcm_md5(samples, 16, 2):
        si.write(b, 8)
    sib = si.bytes()
    with open(path, 'wb') as f:
        f.write(b'fLaC' + bytes([0x80]) + len(sib).to_bytes(3, 'big') + sib + frames)
    return total


def _tone(rate, freq, seconds, dbfs):
    amp = 10.0 ** (dbfs / 20.0)
    n = int(rate * seconds)
    out = []
    for i in range(n):
        v = _to_pcm(amp * math.sin(2 * math.pi * freq * i / rate))
        out.append((v, v))
    return out


def _sweep(rate, f0, f1, seconds, dbfs):
    amp = 10.0 ** (dbfs / 20.0)
    n = int(rate * seconds)
    k = math.log(f1 / f0)
    out = []
    for i in range(n):
        t = i / rate
        ph = 2 * math.pi * f0 * seconds / k * (math.exp(t / seconds * k) - 1.0)
        v = _to_pcm(amp * math.sin(ph))
        out.append((v, v))
    return out


def gen(outdir, seconds=SECONDS_TONE):
    SECONDS_TONE = seconds
    os.makedirs(outdir, exist_ok=True)
    plan = []
    for rate in (44100, 48000):
        for freq, tag in ((1000, '1k'), (5000, '5k'), (10000, '10k')):
            plan.append(('tone_%s_%d.flac' % (tag, rate), rate, _tone(rate, freq, SECONDS_TONE, -6.0),
                         'sine %d Hz, -6 dBFS, %d s' % (freq, SECONDS_TONE)))
    for rate in (24000, 32000):   # 2:1 and 3:2 into 48 kHz: fixed phase pattern, no slow beat (isolates the 44.1 kHz problem)
        plan.append(('tone_1k_%d.flac' % rate, rate, _tone(rate, 1000, SECONDS_TONE, -6.0),
                     'sine 1000 Hz, -6 dBFS at a %d Hz source rate' % rate))
    for freq, tag in ((1000, '1k'), (5000, '5k')):
        plan.append(('tone_%s_22050.flac' % tag, 22050, _tone(22050, freq, SECONDS_TONE, -6.0),
                     'sine %d Hz at a 22.05 kHz source rate' % freq))
    lv = []
    for db in LEVEL_STEPS:
        lv += _tone(44100, 1000, 3, db)
    plan.append(('levels_44100.flac', 44100, lv,
                 '1 kHz at %s dBFS, 3 s each' % ', '.join('%g' % d for d in LEVEL_STEPS)))
    plan.append(('sweep_44100.flac', 44100, _sweep(44100, 20.0, 20000.0, 20, -6.0),
                 'log sweep 20 Hz to 20 kHz, 20 s, -6 dBFS'))
    plan.append(('silence_44100.flac', 44100, [(0, 0)] * (44100 * seconds), '%d s of digital silence' % seconds))
    print('%-22s %-7s %s' % ('file', 'size', 'content'))
    for name, rate, samples, what in plan:
        path = os.path.join(outdir, name)
        _write_flac(path, samples, rate)
        print('%-22s %5.1f MB %s' % (name, os.path.getsize(path) / 1e6, what))
    print('\nCopy the folder into the SD card at Assets/<core>/common/ (a core with a library needs it re-scanned:'
          '\n  python3 tools/sync_media.py ... --library), or play the files from the file browser of the core you test.'
          '\nThe 22.05 kHz files are untested on the player: if one is refused, note it and skip it.')


# ----------------------------------------------------------------------------- WAV reading

def read_wav(path, channel=0):
    """Minimal RIFF reader: PCM 8/16/24/32-bit and float 32/64, any channel count. Returns (rate, samples in [-1,1])."""
    with open(path, 'rb') as f:
        data = f.read()
    if data[:4] != b'RIFF' or data[8:12] != b'WAVE':
        raise ValueError('not a RIFF/WAVE file: ' + path)
    pos, fmt, pcm = 12, None, None
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack('<I', data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b'fmt ':
            tag, nch, rate, _, _, bits = struct.unpack('<HHIIHH', body[:16])
            if tag == 0xFFFE and len(body) >= 26:
                tag = struct.unpack('<H', body[24:26])[0]
            fmt = (tag, nch, rate, bits)
        elif cid == b'data':
            pcm = body
            break
        pos += 8 + size + (size & 1)
    if fmt is None or pcm is None:
        raise ValueError('missing fmt or data chunk')
    tag, nch, rate, bits = fmt
    if channel >= nch:
        raise ValueError('file has %d channel(s), asked for channel %d' % (nch, channel))
    width = bits // 8
    n = len(pcm) // (width * nch)
    out = []
    if tag == 1 and bits == 16:
        vals = struct.unpack('<%dh' % (n * nch), pcm[:n * nch * 2])
        out = [vals[i * nch + channel] / 32768.0 for i in range(n)]
    elif tag == 1 and bits == 24:
        for i in range(n):
            o = (i * nch + channel) * 3
            v = int.from_bytes(pcm[o:o + 3], 'little', signed=True)
            out.append(v / 8388608.0)
    elif tag == 1 and bits == 32:
        vals = struct.unpack('<%di' % (n * nch), pcm[:n * nch * 4])
        out = [vals[i * nch + channel] / 2147483648.0 for i in range(n)]
    elif tag == 1 and bits == 8:
        out = [(pcm[i * nch + channel] - 128) / 128.0 for i in range(n)]
    elif tag == 3 and bits == 32:
        vals = struct.unpack('<%df' % (n * nch), pcm[:n * nch * 4])
        out = [vals[i * nch + channel] for i in range(n)]
    elif tag == 3 and bits == 64:
        vals = struct.unpack('<%dd' % (n * nch), pcm[:n * nch * 8])
        out = [vals[i * nch + channel] for i in range(n)]
    else:
        raise ValueError('unsupported WAV format tag %d, %d bits' % (tag, bits))
    return rate, out


# ----------------------------------------------------------------------------- FFT and analysis

def _fft_py(x):
    n = len(x)
    j = 0
    x = list(x)
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j ^= bit
        if i < j:
            x[i], x[j] = x[j], x[i]
    size = 2
    while size <= n:
        half = size >> 1
        w = [cmath.exp(-2j * math.pi * k / size) for k in range(half)]
        for start in range(0, n, size):
            for k in range(half):
                a = x[start + k]
                b = x[start + k + half] * w[k]
                x[start + k] = a + b
                x[start + k + half] = a - b
        size <<= 1
    return x


def spectrum(samples):
    """Windowed power spectrum. Returns (list of |X|^2 for bins 0..N/2, sum(w), sum(w^2))."""
    n = len(samples)
    w = [0.35875 - 0.48829 * math.cos(2 * math.pi * i / (n - 1)) + 0.14128 * math.cos(4 * math.pi * i / (n - 1))
         - 0.01168 * math.cos(6 * math.pi * i / (n - 1)) for i in range(n)]      # 4-term Blackman-Harris
    sw = sum(w)
    sw2 = sum(v * v for v in w)
    if _np is not None:
        xs = _np.asarray(samples) * _np.asarray(w)
        X = _np.fft.rfft(xs)
        return [float(abs(v)) ** 2 for v in X], sw, sw2
    X = _fft_py([complex(samples[i] * w[i], 0) for i in range(n)])
    return [abs(X[k]) ** 2 for k in range(n // 2 + 1)], sw, sw2


def band_amp(power_sum, n, sw2):
    """Peak amplitude of a sine whose windowed power (positive-frequency lobe) is `power_sum`.
    Summing the whole lobe removes the scalloping loss a single bin would have when the tone is not bin-centred."""
    return math.sqrt(4.0 * power_sum / (n * sw2))


def db(x):
    return 10.0 * math.log10(x) if x > 1e-30 else -300.0


def pick_segment(samples, max_len):
    """The steady middle of the loudest region, as a power-of-two length no longer than max_len."""
    blk = 4096
    rms = []
    for s in range(0, len(samples) - blk + 1, blk):
        seg = samples[s:s + blk]
        rms.append(math.sqrt(sum(v * v for v in seg) / blk))
    if not rms or max(rms) <= 1e-9:
        return samples[:max_len] if len(samples) >= max_len else samples
    thr = 0.5 * max(rms)
    idx = [i for i, r in enumerate(rms) if r >= thr]
    lo, hi = idx[0] * blk, (idx[-1] + 1) * blk
    trim = (hi - lo) // 10
    lo, hi = lo + trim, hi - trim
    n = 1
    while n * 2 <= min(hi - lo, max_len):
        n *= 2
    mid = (lo + hi) // 2
    start = max(0, min(len(samples) - n, mid - n // 2))
    return samples[start:start + n]


def analyze_tone(samples, rate, freq=None, nfft=1 << 16, band=4, harmonics=5, spur_count=5):
    seg = pick_segment(samples, nfft)
    n = len(seg)
    p, sw, sw2 = spectrum(seg)
    binhz = rate / float(n)
    lo_bin = max(1, int(20.0 / binhz))            # ignore DC and rumble below 20 Hz
    if freq is None:
        k0 = max(range(lo_bin, len(p)), key=lambda k: p[k])
    else:
        c = int(round(freq / binhz))
        w = max(band, int(0.01 * freq / binhz))
        k0 = max(range(max(lo_bin, c - w), min(len(p) - 1, c + w) + 1), key=lambda k: p[k])
    f0 = k0 * binhz
    excl = set()

    def take(k):
        s = set(range(max(lo_bin, k - band), min(len(p) - 1, k + band) + 1))
        excl.update(s)
        return sum(p[i] for i in s)

    fund = take(k0)
    harm = 0.0
    for h in range(2, harmonics + 1):
        kh = int(round(h * f0 / binhz))
        if kh + band < len(p):
            harm += take(kh)
    total = sum(p[lo_bin:])
    rest = max(total - fund, 1e-30)
    rest_bins = [k for k in range(lo_bin, len(p)) if k not in excl]
    # local maxima among the remaining bins, strongest first
    peaks = [k for k in rest_bins if p[k] > 0 and (k == 0 or p[k] >= p[k - 1]) and (k + 1 >= len(p) or p[k] >= p[k + 1])]
    peaks.sort(key=lambda k: p[k], reverse=True)
    chosen = []
    for k in peaks:
        if all(abs(k - c) > 2 * band for c in chosen):
            chosen.append(k)
        if len(chosen) >= spur_count:
            break
    fund_db = db(fund)
    med = sorted(p[k] for k in rest_bins)[len(rest_bins) // 2] if rest_bins else 0.0
    peak_amp = band_amp(fund, n, sw2)                  # peak amplitude of the sine, from the whole lobe
    return {
        'rate': rate, 'fft_len': n, 'bin_hz': binhz,
        'freq_hz': round(f0, 2),
        'level_dbfs': round(20.0 * math.log10(max(peak_amp, 1e-15)), 2),
        'sinad_db': round(fund_db - db(rest), 2),
        'thd_db': round(db(max(harm, 1e-30)) - fund_db, 2),
        'floor_dbfs': round(20.0 * math.log10(max(2.0 * math.sqrt(med) / sw, 1e-15)), 2),   # per-bin level of the median remaining bin
        'spurs': [{'hz': round(k * binhz, 1), 'dbc': round(db(p[k]) - fund_db, 2)} for k in chosen],
    }


def analyze_sweep(samples, rate, points=(50, 100, 200, 500, 1000, 2000, 5000, 10000, 15000, 18000, 20000)):
    """Frequency response from a log sweep. A fast chirp smears across FFT bins, so this measures per short
    window: level from the RMS (a sine's peak is sqrt(2) x RMS) and frequency from the zero-crossing rate.
    Reported at the requested frequencies relative to 1 kHz."""
    n = 8192
    hop = n // 2
    rows = []
    for s in range(0, len(samples) - n, hop):
        seg = samples[s:s + n]
        peak = max(abs(v) for v in seg)
        if peak < 1e-4:
            continue
        mean = sum(seg) / n
        seg = [v - mean for v in seg]
        cross = sum(1 for i in range(1, n) if (seg[i - 1] < 0) != (seg[i] < 0))
        freq = cross / 2.0 * rate / n
        rms = math.sqrt(sum(v * v for v in seg) / n)
        rows.append((freq, 20.0 * math.log10(max(rms * math.sqrt(2.0), 1e-15))))
    if not rows:
        raise ValueError('no signal found in the sweep recording')

    def at(f):
        return min(rows, key=lambda r: abs(math.log(max(r[0], 1.0) / f)))
    ref = at(1000.0)[1]
    return {'reference_1k_dbfs': round(ref, 2),
            'response': [{'hz': int(f), 'db_rel_1k': round(at(f)[1] - ref, 2), 'nearest_hz': int(at(f)[0])} for f in points]}


# ----------------------------------------------------------------------------- reporting

def print_tone(r, name=''):
    print('%s' % (name or 'tone'))
    print('  tone %.2f Hz   level %.2f dBFS   SINAD %.2f dB   THD %.2f dB   floor %.2f dBFS   (bin %.3f Hz, N=%d)' % (
        r['freq_hz'], r['level_dbfs'], r['sinad_db'], r['thd_db'], r['floor_dbfs'], r['bin_hz'], r['fft_len']))
    for s in r['spurs']:
        print('    spur %8.1f Hz   %7.2f dBc' % (s['hz'], s['dbc']))


def compare(a, b):
    print('%-16s %12s %12s %12s' % ('metric', 'baseline', 'later', 'change'))
    for key in ('freq_hz', 'level_dbfs', 'sinad_db', 'thd_db', 'floor_dbfs'):
        if key in a and key in b:
            print('%-16s %12.2f %12.2f %+12.2f' % (key, a[key], b[key], b[key] - a[key]))
    print('\nstrongest spurs (dBc), baseline vs later:')
    for i in range(max(len(a.get('spurs', [])), len(b.get('spurs', [])))):
        sa = a['spurs'][i] if i < len(a.get('spurs', [])) else None
        sb = b['spurs'][i] if i < len(b.get('spurs', [])) else None
        print('  %s   |   %s' % ('%8.1f Hz %7.2f' % (sa['hz'], sa['dbc']) if sa else ' ' * 18,
                                 '%8.1f Hz %7.2f' % (sb['hz'], sb['dbc']) if sb else ' ' * 18))


# ----------------------------------------------------------------------------- self test

def _resample_nearest(src_rate, src, out_rate):
    r = src_rate / float(out_rate)
    n = int(len(src) / r) - 4
    return [src[int(math.floor(k * r))] for k in range(n)]


def _resample_cubic(src_rate, src, out_rate):
    r = src_rate / float(out_rate)
    n = int(len(src) / r) - 4
    out = []
    for k in range(n):
        t = k * r
        m = int(math.floor(t))
        u = t - m
        p0, p1, p2, p3 = src[m - 1], src[m], src[m + 1], src[m + 2]
        out.append(p1 + 0.5 * u * (p2 - p0 + u * (2 * p0 - 5 * p1 + 4 * p2 - p3 + u * (3 * (p1 - p2) + p3 - p0))))
    return out


def selftest(nfft=1 << 15, verbose=True):
    """Checks the analyser against known signals. Returns a list of failure strings (empty = pass)."""
    fails = []
    src_rate, out_rate, f = 44100, 48000, 1000.0
    n_src = out_rate * 3
    src = [0.5 * math.sin(2 * math.pi * f * i / src_rate) for i in range(n_src)]
    # 1. clean 48 kHz tone quantised to 16 bits: near the quantisation limit
    clean = [round(0.5 * math.sin(2 * math.pi * f * i / out_rate) * 32767) / 32767.0 for i in range(out_rate * 3)]
    r = analyze_tone(clean, out_rate, f, nfft)
    if verbose:
        print_tone(r, 'clean 48 kHz tone (16-bit)')
    if abs(r['level_dbfs'] - (-6.02)) > 0.15:
        fails.append('level of a -6.02 dBFS tone read %.2f' % r['level_dbfs'])
    if r['sinad_db'] < 70:
        fails.append('clean tone SINAD only %.1f dB' % r['sinad_db'])
    # 2. nearest-neighbour 44.1 -> 48 kHz: the plan predicts about 28 dB and an image near 4.9 kHz
    nn = _resample_nearest(src_rate, src, out_rate)
    r = analyze_tone(nn, out_rate, f, nfft)
    if verbose:
        print_tone(r, 'nearest-neighbour 44.1 -> 48 kHz, 1 kHz')
    if not 24.0 <= r['sinad_db'] <= 32.0:
        fails.append('nearest-neighbour SINAD %.1f dB outside 24..32 (the host model predicted about 28)' % r['sinad_db'])
    if not any(abs(s['hz'] - 4900.0) < 40.0 for s in r['spurs']):
        fails.append('no image found near 4900 Hz; spurs were %s' % [s['hz'] for s in r['spurs']])
    # 3. cubic interpolation should be far cleaner
    cu = _resample_cubic(src_rate, src, out_rate)
    r = analyze_tone(cu, out_rate, f, nfft)
    if verbose:
        print_tone(r, 'cubic 44.1 -> 48 kHz, 1 kHz')
    if r['sinad_db'] < 55:
        fails.append('cubic SINAD only %.1f dB' % r['sinad_db'])
    # 4. sweep analysis on a synthetic flat sweep must come out flat
    sw_rate = 44100
    ph = lambda t: 2 * math.pi * 20.0 * 20.0 / math.log(1000.0) * (math.exp(t / 20.0 * math.log(1000.0)) - 1.0)
    sweep = [0.5 * math.sin(ph(i / sw_rate)) for i in range(sw_rate * 20)]
    rs = analyze_sweep(sweep, sw_rate)
    for row in rs['response']:
        if row['hz'] <= 15000 and abs(row['db_rel_1k']) > 1.0:
            fails.append('flat sweep read %+.2f dB at %d Hz' % (row['db_rel_1k'], row['hz']))
    # 5. WAV reader round trip (16-bit and float32)
    import tempfile
    for tag, bits, packer in ((1, 16, lambda v: struct.pack('<h', int(round(v * 32767)))),
                              (3, 32, lambda v: struct.pack('<f', v))):
        body = b''.join(packer(v) for v in clean[:2000])
        hdr = struct.pack('<4sI4s4sIHHIIHH4sI', b'RIFF', 36 + len(body), b'WAVE', b'fmt ', 16, tag, 1, out_rate,
                          out_rate * bits // 8, bits // 8, bits, b'data', len(body))
        with tempfile.NamedTemporaryFile(suffix='.wav', delete=False) as t:
            t.write(hdr + body)
        try:
            rate, got = read_wav(t.name)
            if rate != out_rate or len(got) != 2000 or max(abs(a - b) for a, b in zip(got, clean[:2000])) > 1e-3:
                fails.append('WAV reader round trip failed for format %d/%d' % (tag, bits))
        finally:
            os.unlink(t.name)
    return fails


# ----------------------------------------------------------------------------- CLI

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    g = sub.add_parser('gen', help='write the lossless FLAC test files')
    g.add_argument('outdir')
    g.add_argument('--seconds', type=int, default=SECONDS_TONE, help='length of each tone/silence file (default %(default)s)')
    a = sub.add_parser('analyze', help='analyse a recording')
    a.add_argument('wav')
    a.add_argument('--freq', type=float, help='expected tone frequency in Hz (default: strongest peak)')
    a.add_argument('--sweep', action='store_true', help='analyse a log-sweep recording instead of a tone')
    a.add_argument('--channel', type=int, default=0, help='channel of the recording to analyse (0 = left)')
    a.add_argument('--fft', type=int, default=1 << 16, help='FFT length (power of two)')
    a.add_argument('--json', help='write the result to this file (for compare)')
    c = sub.add_parser('compare', help='compare two saved results')
    c.add_argument('baseline')
    c.add_argument('later')
    sub.add_parser('selftest', help='check the analyser against known signals')
    args = ap.parse_args()

    if args.cmd == 'gen':
        gen(args.outdir, args.seconds)
    elif args.cmd == 'analyze':
        rate, x = read_wav(args.wav, args.channel)
        if args.sweep:
            r = analyze_sweep(x, rate)
            print('sweep response (dB relative to 1 kHz; reference level %.2f dBFS)' % r['reference_1k_dbfs'])
            for row in r['response']:
                print('  %6d Hz   %+6.2f dB   (measured at %d Hz)' % (row['hz'], row['db_rel_1k'], row['nearest_hz']))
        else:
            r = analyze_tone(x, rate, args.freq, args.fft)
            print_tone(r, os.path.basename(args.wav))
        if args.json:
            with open(args.json, 'w') as f:
                json.dump(r, f, indent=2)
    elif args.cmd == 'compare':
        with open(args.baseline) as f1, open(args.later) as f2:
            compare(json.load(f1), json.load(f2))
    elif args.cmd == 'selftest':
        fails = selftest()
        if fails:
            print('\nFAIL:\n  ' + '\n  '.join(fails))
            sys.exit(1)
        print('\nselftest PASS')


if __name__ == '__main__':
    main()
