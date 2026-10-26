#!/usr/bin/env python3
"""Cymo C3 host model: the output stage's soft clipper and TPDF dither (docs/features/CYMO_OUTPUT_STAGE_SPEC.md).

Integer-exact, so the RTL can later be checked against it sample for sample. Two blocks, both stateless apart from the dither LFSR:

  softclip(x)  18-bit signed in (the EQ's unclamped result, +-4 full scale), 16-bit signed out.
               |x| <= KNEE passes unchanged. Above the knee a 256-segment piecewise-linear table approximates
               y = KNEE + H*(1 - exp(-(|x|-KNEE)/H)), H = 32767 - KNEE: slope 1 at the knee, asymptote 32767.
  dither15(y, d) 16-bit in, 15-bit out (the DAC slot carries 15 bits, F2): (y + d) >> 1 with d a TPDF value in
               {-1, 0, +1} 16-bit LSB units... generated from two 1-bit LFSR draws summed (triangular).
"""
import math

FS = 32767
KNEE = 29491                      # 0.9 full scale
H = FS - KNEE
SEG_SHIFT = 7                     # 128-unit segments
NSEG = 256                        # covers (|x| - KNEE) up to 32768 (10 H: the curve is within 0.2 LSB of its asymptote beyond that)
IN_MAX = 131071                   # 18-bit signed


def _ideal(u):                    # u = |x| - KNEE >= 0 ; float reference
    return KNEE + H * (1.0 - math.exp(-u / H))


def build_table():
    # NSEG+1 breakpoints, rounded to nearest, forced monotonic and capped at FS
    t = []
    for i in range(NSEG + 1):
        v = int(round(_ideal(i << SEG_SHIFT)))
        if t and v < t[-1]:
            v = t[-1]
        t.append(min(v, FS))
    return t


TABLE = build_table()


def softclip(x):
    a = -x if x < 0 else x
    if a <= KNEE:
        return x
    u = a - KNEE
    i = u >> SEG_SHIFT
    if i >= NSEG:
        y = TABLE[NSEG]
    else:
        f = u & ((1 << SEG_SHIFT) - 1)
        y = TABLE[i] + (((TABLE[i + 1] - TABLE[i]) * f + (1 << (SEG_SHIFT - 1))) >> SEG_SHIFT)
    return -y if x < 0 else y


class Lfsr:
    """32-bit Galois LFSR (taps 32,22,2,1), one new bit per step; the dither draws two bits per sample."""
    def __init__(self, seed=0xACE1ACE1):
        self.s = seed & 0xFFFFFFFF or 1

    def bit(self):
        b = self.s & 1
        self.s >>= 1
        if b:
            self.s ^= 0x80200003
        return b

    def tpdf(self):               # sum of two independent 0/1 draws minus 1: {-1, 0, +1} with p = 1/4, 1/2, 1/4
        return self.bit() + self.bit() - 1


def dither15(y, d):
    """16-bit y plus dither d (16-bit LSBs, TPDF {-1,0,+1}) plus 1 (rounds: a plain floor shift is biased by -0.5 of a 15-bit LSB), then drop one bit; clamp to 15-bit."""
    v = (y + d + 1) >> 1
    return max(-16384, min(16383, v))
