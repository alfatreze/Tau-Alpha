#!/usr/bin/env python3
"""Properties the C3 output-stage model must have before any RTL is written (docs/features/CYMO_OUTPUT_STAGE_SPEC.md)."""
import math, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools", "lab"))
import cymo_out_model as m

fails = 0
def check(name, ok, info=""):
    global fails
    print(("PASS " if ok else "FAIL ") + name + (" " + info if info else ""))
    fails += 0 if ok else 1

# softclip: exactly transparent up to and including the knee, both signs
check("softclip transparent below the knee", all(m.softclip(x) == x for x in range(-m.KNEE, m.KNEE + 1)))
# monotonic non-decreasing over the whole 18-bit input
prev = m.softclip(-131072)
mono = True
for x in range(-131071, 131072):
    y = m.softclip(x)
    if y < prev: mono = False; break
    prev = y
check("softclip monotonic over the 18-bit range", mono)
# never beyond full scale, odd symmetry
top = max(abs(m.softclip(x)) for x in range(-131072, 131072))
check("softclip never exceeds +-32767", top <= m.FS, f"(max {top})")
check("softclip odd-symmetric", all(m.softclip(-x) == -m.softclip(x) for x in range(0, 131072, 7)))
# continuity at the knee and error against the ideal curve
err = max(abs(m.softclip(m.KNEE + u) - m._ideal(u)) for u in range(0, 131072 - m.KNEE))
check("softclip within 1.5 LSB (16-bit) of the ideal curve", err <= 1.5, f"(worst {err:.2f})")
check("softclip step at the knee is <= 1 LSB", abs(m.softclip(m.KNEE + 1) - m.softclip(m.KNEE)) <= 1)
# a 0 dBFS-ish peak (+-32767 sine plus 3 dB EQ boost -> 46341) lands inside full scale
check("a +3 dB peak (46341) is rounded, not hard-clipped", m.KNEE < m.softclip(46341) < m.FS)

# dither: statistics and decorrelation
lf = m.Lfsr()
n = 200000
ds = [lf.tpdf() for _ in range(n)]
mean = sum(ds) / n
var = sum(d * d for d in ds) / n
check("TPDF dither mean ~ 0", abs(mean) < 0.01, f"(mean {mean:.4f})")
check("TPDF dither variance ~ 0.5 LSB^2", abs(var - 0.5) < 0.01, f"(var {var:.4f})")
cnt = {k: ds.count(k) / n for k in (-1, 0, 1)}
check("TPDF shape 1/4 1/2 1/4", abs(cnt[-1] - .25) < .01 and abs(cnt[0] - .5) < .01 and abs(cnt[1] - .25) < .01, str({k: round(v, 3) for k, v in cnt.items()}))

def harmonics(sig, f0, fs, nharm=8):
    """power in the 2nd..nharm-th harmonics relative to the fundamental (dB), by single-bin DFT with a Hann window"""
    N = len(sig)
    w = [0.5 - 0.5 * math.cos(2 * math.pi * i / N) for i in range(N)]
    def bin_(f):
        re = sum(sig[i] * w[i] * math.cos(2 * math.pi * f * i / fs) for i in range(N))
        im = sum(sig[i] * w[i] * math.sin(2 * math.pi * f * i / fs) for i in range(N))
        return re * re + im * im
    p1 = bin_(f0)
    ph = sum(bin_(f0 * k) for k in range(2, nharm + 1) if f0 * k < fs / 2)
    return 10 * math.log10(ph / p1)

# a quiet 1 kHz tone, 3 LSB (16-bit units) amplitude with a fractional offset: truncation distorts, dither decorrelates
fs = 48000; N = 8192
f0 = 1000.0 * 8 / 8  # exact bin not needed with a Hann window
src = [3.3 * math.sin(2 * math.pi * f0 * i / fs) + 0.37 for i in range(N)]
lf = m.Lfsr(0x1234567)
plain = [(int(math.floor(v)) >> 1) for v in src]
dith = [m.dither15(int(math.floor(v)), lf.tpdf()) for v in src]
hp, hd = harmonics(plain, f0, fs), harmonics(dith, f0, fs)
check("dither lowers harmonic distortion of a 3-LSB tone vs plain truncation", hd < hp, f"(truncated {hp:.1f} dB, dithered {hd:.1f} dB re fundamental)")
# error decorrelation: with dither the error has no DC offset against the signal's fractional part
e_plain = sum(((p * 2) - v) for p, v in zip(plain, src)) / N
e_dith = sum(((p * 2) - v) for p, v in zip(dith, src)) / N
check("dither path has no DC bias (plain truncation is biased)", abs(e_dith) < 0.1 and abs(e_plain) > 0.5, f"(bias plain {e_plain:.3f}, dithered {e_dith:.3f} 16-bit LSB)")
# silence stays silent-ish: zero in, dither noise only, bounded to 1 LSB of the 15-bit output
lf = m.Lfsr(99)
sil = [m.dither15(0, lf.tpdf()) for _ in range(20000)]
check("zero input gives at most +-1 output LSB (15-bit)", all(0 <= s <= 1 for s in sil), f"(values {sorted(set(sil))})")
sys.exit(1 if fails else 0)
