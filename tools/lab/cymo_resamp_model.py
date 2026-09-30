#!/usr/bin/env python3
"""Cymo resampler design model (docs/features/CYMO_AUDIO_ENGINE.md section 15 / Appendix).

Purpose: pick a tap count and window for a real hardware polyphase FIR resampler (44.1 kHz ->
48 kHz, exact ratio 160:147 -- the neoge/pocket-mp3 architecture, github.com/neoge/pocket-mp3)
BEFORE spending any RTL or Quartus time on it, following this project's own "prove it in a host
model first" discipline (the same one used for the MP3 filterbank and FLAC LPC hardware kernels).

This is NOT the idealised float model in the Appendix above -- coefficients are quantised to
16-bit signed fixed point (the same width `resampler.sv`'s own `coef_data` port uses), so the
predicted SINAD reflects what a real 16-bit coefficient ROM would actually produce, not what an
infinite-precision resampler would. Standard library only (uses numpy for speed when installed).

Usage:
    python3 tools/lab/cymo_resamp_model.py sweep
    python3 tools/lab/cymo_resamp_model.py sweep --taps 8,16,24,32,48,64 --windows rect,hamming,blackman,kaiser
    python3 tools/lab/cymo_resamp_model.py detail --taps 32 --window kaiser --freq 5000
    python3 tools/lab/cymo_resamp_model.py resources --taps 8,16,24,32,48,64

Algorithm (matches the RTL this would become): P=160, Q=147 phase banks, `phase` advances by Q
mod P per output sample (`phase += Q; if phase >= P: phase -= P; advance input`), bank index used
is `phase` BEFORE the increment -- literally `resampler.sv`'s own scheme. A prototype low-pass of
length TAPS*P is designed at cutoff min(44100,48000)/2, windowed, then decomposed into P banks of
TAPS coefficients each (`bank[p][t] = h[t*P + p]`), each independently normalised so its own DC gain
is unity (closer to what a resampler filter actually needs than a single global normalisation).
"""
import argparse
import math
import sys

try:
    import numpy as np
    HAVE_NUMPY = True
except ImportError:
    HAVE_NUMPY = False

P, Q = 160, 147          # exact 44100 -> 48000 ratio (44100*160 == 48000*147 == 7,056,000)
FS_IN, FS_OUT = 44100, 48000
COEF_BITS = 16           # matches resampler.sv's coef_data port
M10K_BITS = 10240        # Cyclone V M10K usable capacity


def sinc(x):
    return 1.0 if x == 0.0 else math.sin(math.pi * x) / (math.pi * x)


def window(name, n, i):
    """Standard window functions, i in [0, n-1]."""
    if n <= 1:
        return 1.0
    x = i / (n - 1)
    if name == "rect":
        return 1.0
    if name == "hamming":
        return 0.54 - 0.46 * math.cos(2 * math.pi * x)
    if name == "blackman":
        return 0.42 - 0.5 * math.cos(2 * math.pi * x) + 0.08 * math.cos(4 * math.pi * x)
    if name == "kaiser":
        beta = 8.6   # a reasonable stopband/transition tradeoff for this filter length range
        alpha = (n - 1) / 2.0
        t = (i - alpha) / alpha if alpha else 0.0
        arg = beta * math.sqrt(max(0.0, 1.0 - t * t))
        return _bessel_i0(arg) / _bessel_i0(beta)
    raise ValueError("unknown window %r" % name)


def _bessel_i0(x):
    """Modified Bessel function I0, series expansion -- good enough for Kaiser windows."""
    s, term, k = 1.0, 1.0, 1
    half_x = x / 2.0
    while True:
        term *= (half_x / k) ** 2
        s += term
        if term < 1e-12 * s:
            return s
        k += 1


def design_prototype(taps_per_bank, win_name):
    """The full-length prototype low-pass, length taps_per_bank*P, cutoff at
    min(FS_IN, FS_OUT)/2 in the UPSAMPLED (by P) domain -- the standard polyphase design."""
    n = taps_per_bank * P
    fc = min(FS_IN, FS_OUT) / 2.0          # the real Nyquist limit after resampling
    fc_norm = fc / (FS_IN * P) * 2.0        # normalised cutoff in the P*FS_IN upsampled domain
    center = (n - 1) / 2.0
    h = []
    for i in range(n):
        x = i - center
        ideal = fc_norm * sinc(fc_norm * x)
        h.append(ideal * window(win_name, n, i))
    return h


def build_banks(taps_per_bank, win_name):
    """Decompose the prototype into P banks of taps_per_bank coefficients each, quantised to
    signed COEF_BITS-bit fixed point, each bank independently normalised to unity DC gain
    (matches how a real coefficient ROM would be generated per-bank)."""
    h = design_prototype(taps_per_bank, win_name)
    banks_f = [[0.0] * taps_per_bank for _ in range(P)]
    for i, v in enumerate(h):
        bank, tap = i % P, i // P
        if tap < taps_per_bank:
            banks_f[bank][tap] = v
    scale = (1 << (COEF_BITS - 1)) - 1
    banks_q = []
    for bank in banks_f:
        dc = sum(bank)
        norm = [c / dc if dc != 0 else c for c in bank] if dc != 0 else bank
        q = [max(-scale - 1, min(scale, round(c * scale))) for c in norm]
        banks_q.append([v / scale for v in q])   # back to float, but at real 16-bit resolution
    return banks_q


def resample_tone(freq, seconds, banks_q, taps_per_bank):
    """Runs the exact RTL algorithm (phase accumulator mod P, bank selects, FIR MAC over the
    ring history) on a synthetic tone, returns the output sample stream at FS_OUT."""
    n_in = int(FS_IN * seconds)
    hist = [0.0] * taps_per_bank   # newest-first ring, conceptually; we just re-slice each input
    out = []
    phase = 0
    in_pos = 0
    # Precompute the whole input tone (cheap enough for a lab tool).
    tone = [math.sin(2 * math.pi * freq * i / FS_IN) for i in range(n_in + taps_per_bank + 2)]
    while in_pos < n_in:
        bank = banks_q[phase]
        # tap t pairs with the sample (taps_per_bank-1-t) positions behind the newest -- matches
        # resampler.sv's own hist_head-relative indexing, simplified here since we have the whole
        # tone precomputed: newest input consumed so far is index in_pos-1.
        acc = 0.0
        for t in range(taps_per_bank):
            idx = in_pos - 1 - t
            if 0 <= idx < len(tone):
                acc += bank[t] * tone[idx]
        out.append(acc)
        phase += Q
        if phase >= P:
            phase -= P
            in_pos += 1
    return out


def sinad_db(out_samples, freq):
    """Goertzel-free SINAD via a plain DFT at the exact bin nearest freq, matching this project's
    other lab tools' approach (no numpy dependency required, numpy used only to go faster)."""
    n = len(out_samples)
    if HAVE_NUMPY:
        x = np.asarray(out_samples, dtype=np.float64)
        w = np.hanning(n)
        xf = x * w
        spec = np.fft.rfft(xf)
        power = (np.abs(spec) ** 2)
        bin_hz = FS_OUT / n
        k = int(round(freq / bin_hz))
        # tone power: sum a few bins around k to capture windowed leakage, same idea as cymo_loopback.py
        lo, hi = max(0, k - 3), min(len(power), k + 4)
        tone_p = power[lo:hi].sum()
        total_p = power.sum()
        noise_p = max(total_p - tone_p, 1e-30)
        return 10 * math.log10(tone_p / noise_p)
    # Pure-Python DFT fallback (slow, only used if numpy is absent) -- limit N for sanity.
    n = min(n, 1 << 14)
    xs = out_samples[:n]
    bin_hz = FS_OUT / n
    k = int(round(freq / bin_hz))

    def goertzel(bin_k):
        w = 2 * math.pi * bin_k / n
        cw, sw = math.cos(w), math.sin(w)
        coeff = 2 * cw
        s0 = s1 = s2 = 0.0
        for x in xs:
            s0 = x + coeff * s1 - s2
            s2, s1 = s1, s0
        re = s1 - s2 * cw
        im = s2 * sw
        return re * re + im * im

    tone_p = sum(goertzel(k + d) for d in (-1, 0, 1))
    total_p = sum(x * x for x in xs) * n / 2.0   # Parseval, rough but consistent for a ranking table
    noise_p = max(total_p - tone_p, 1e-30)
    return 10 * math.log10(tone_p / noise_p)


def m10k_estimate(taps_per_bank):
    bits = P * taps_per_bank * COEF_BITS
    return bits, math.ceil(bits / M10K_BITS)


def cmd_sweep(a):
    taps_list = [int(t) for t in a.taps.split(",")]
    windows = a.windows.split(",")
    freqs = [1000, 5000, 10000, 15000, 18000]
    seconds = a.seconds
    print("Polyphase FIR resampler, 44.1kHz -> 48kHz (P=%d/Q=%d), %d-bit quantised coefficients" % (P, Q, COEF_BITS))
    print("(compare against the Appendix's idealised float model and section 3's nearest-neighbour baseline)\n")
    for win in windows:
        print("== window: %s ==" % win)
        header = "taps | " + " | ".join("%6d Hz" % f for f in freqs) + " | M10K bits | M10K blocks"
        print(header)
        for taps in taps_list:
            banks = build_banks(taps, win)
            row = []
            for f in freqs:
                out = resample_tone(f, seconds, banks, taps)
                row.append(sinad_db(out, f))
            bits, blocks = m10k_estimate(taps)
            print("%4d | " % taps + " | ".join("%9.1f" % v for v in row) + " | %9d | %11d" % (bits, blocks))
        print()


def cmd_detail(a):
    banks = build_banks(a.taps, a.window)
    out = resample_tone(a.freq, a.seconds, banks, a.taps)
    db = sinad_db(out, a.freq)
    bits, blocks = m10k_estimate(a.taps)
    print("taps=%d window=%s freq=%d Hz -> SINAD %.2f dB" % (a.taps, a.window, a.freq, db))
    print("coefficient ROM: %d banks x %d taps x %d bits = %d bits (%d M10K blocks)" % (P, a.taps, COEF_BITS, bits, blocks))
    print("1 time-multiplexed DSP multiplier (one MAC per clock, %d cycles/output sample)" % a.taps)


def cmd_resources(a):
    taps_list = [int(t) for t in a.taps.split(",")]
    print("taps | coefficient bits | M10K blocks | DSP | cycles/output sample (MAC @ 1/clk)")
    for taps in taps_list:
        bits, blocks = m10k_estimate(taps)
        print("%4d | %17d | %11d | %3d | %d" % (taps, bits, blocks, 1, taps))
    total_m10k = 308  # Cyclone V 5CEBA4, this project's current device
    used = 240        # v0.6.0-alpha.1 shipped bitstream, B-458
    print("\nFor reference: this project's shipped bitstream uses %d/%d M10K blocks (%d free)." %
          (used, total_m10k, total_m10k - used))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("sweep", help="SINAD table across tap counts and windows")
    s.add_argument("--taps", default="8,16,24,32,48,64")
    s.add_argument("--windows", default="rect,hamming,blackman,kaiser")
    s.add_argument("--seconds", type=float, default=0.4)
    s.set_defaults(fn=cmd_sweep)

    d = sub.add_parser("detail", help="single tap-count/window/frequency result")
    d.add_argument("--taps", type=int, default=32)
    d.add_argument("--window", default="kaiser")
    d.add_argument("--freq", type=float, default=5000)
    d.add_argument("--seconds", type=float, default=0.4)
    d.set_defaults(fn=cmd_detail)

    r = sub.add_parser("resources", help="M10K/DSP/cycle cost per tap count, no simulation")
    r.add_argument("--taps", default="8,16,24,32,48,64")
    r.set_defaults(fn=cmd_resources)

    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
