#!/usr/bin/env python3
"""Bit-exact integer model of the Halcyon engine (src/fpga/core/tau_halcyon.sv) and the
golden-vector generator for sim/tb_tau_halcyon.v.

Same arithmetic as the RTL: Q2.22 coefficients, state Q20.16 in 36 bits (wrapping), 64-bit accumulator,
round-to-nearest, output clamp (never wrap), shadow bank swapped at a sample boundary, state-clear sweep,
nact == 0 or bypass = true bypass with the engine state kept warm (nact == 0 skips the engine).
Usage: halcyon_engine_model.py --vectors FILE [--w 16|24]   (selftest without arguments)
"""
import math, sys, random

NST = 17
SW, FC, FS = 36, 22, 16
PRE = NST * 5


def wrap(v, bits):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v >> (bits - 1) else v


class Engine:
    def __init__(self, w=16, wo=None, ifb=None, ofb=None):
        # w / wo: input / output width; ifb / ofb: the fractional bits below the 16-bit LSB (the rest above 16 are headroom at the same LSB).
        # Legacy default (no wo, no ifb): every bit above 16 is fractional, as the 24-bit vectors use.
        wo = wo or w
        self.ifb = (w - 16) if ifb is None else ifb
        self.ofb = (wo - 16) if ofb is None else ofb
        self.w = wo                          # output width (clamp)
        self.shi = 16 - self.ifb             # input to state
        self.sh = 16 - self.ofb              # state to output
        self.bank = 0
        self.cm = [[0] * 128 for _ in range(2)]
        self.nact = 0
        self.pend = None
        self.state = [[[0] * 4 for _ in range(NST)] for _ in range(2)]

    def write(self, idx, val):              # shadow bank only
        self.cm[1 - self.bank][idx] = val

    def commit(self, nact):
        self.pend = nact

    def clear(self):
        self.state = [[[0] * 4 for _ in range(NST)] for _ in range(2)]

    def _rnd(self, acc):
        return wrap((wrap(acc, 64) + (1 << (FC - 1))) >> FC, SW)

    def _clamp(self, v):
        w = wrap(v + (1 << (self.sh - 1)), SW) >> self.sh
        mx, mn = (1 << (self.w - 1)) - 1, -(1 << (self.w - 1))
        return max(mn, min(mx, w))

    def sample(self, xl, xr, bypass):
        if self.pend is not None:
            self.bank ^= 1
            self.nact = self.pend
            self.pend = None
        out = []
        if self.nact == 0:
            return self._clamp(wrap(xl << self.shi, SW)), self._clamp(wrap(xr << self.shi, SW))
        cm = self.cm[self.bank]
        for ch, x in enumerate((xl, xr)):
            smp = wrap(x << self.shi, SW)
            for s in range(self.nact):
                b0, b1, b2, a1, a2 = cm[s * 5:s * 5 + 5]
                x1, x2, y1, y2 = self.state[ch][s]
                acc = b0 * smp + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
                y = self._rnd(acc)
                self.state[ch][s] = [smp, x1, y, y1]
                smp = y
            smp = self._rnd(cm[PRE] * smp)
            out.append(self._clamp(smp))
        return (self._clamp(wrap(xl << self.shi, SW)), self._clamp(wrap(xr << self.shi, SW))) if bypass else (out[0], out[1])


def q(x):
    return int(round(x * (1 << FC)))


def rbj(kind, f, qf, gain_db, fs=48000.0):
    A = 10 ** (gain_db / 40.0)
    w0 = 2 * math.pi * f / fs
    c, s = math.cos(w0), math.sin(w0)
    al = s / (2 * qf)
    if kind == "peak":
        b = [1 + al * A, -2 * c, 1 - al * A]
        a = [1 + al / A, -2 * c, 1 - al / A]
    else:
        sq = 2 * math.sqrt(A) * al
        if kind == "low":
            b = [A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq)]
            a = [(A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq]
        else:
            b = [A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq)]
            a = [(A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq]
    return [q(b[0] / a[0]), q(b[1] / a[0]), q(b[2] / a[0]), q(a[1] / a[0]), q(a[2] / a[0])]


def bank_set(nact, seed, pre_db):
    rnd = random.Random(seed)
    words = []
    kinds = ["low", "peak", "peak", "peak", "peak", "high"]
    for s in range(nact):
        k = kinds[s % 6]
        f = [100, 220, 1800, 3500, 6500, 10000][s % 6] * (1 + 0.07 * (s // 6))
        words += rbj(k, f, 0.7 + 0.1 * (s % 5), rnd.uniform(-9, 9))
    words += [0] * ((NST - nact) * 5)
    words.append(q(10 ** (pre_db / 20)))
    return words                           # NST*5 + 1 words, index PRE last


def stim(n, scale, seed, amp=1.0):
    r = random.Random(seed)
    out = []
    for i in range(n):
        if i == 0:
            v = 20000
        elif i < 20:
            v = 0
        elif i % 3 == 0:
            v = int(31000 * math.sin(2 * math.pi * 16000 * i / 48000))
        else:
            v = r.randint(-30000, 30000)
        out.append(int(v * amp))
    return [v * scale + r.randint(0, scale - 1) if scale > 1 else v for v in out]


def vectors(path, w, wo=None, ifb=None, ofb=None):
    """Writes the golden vectors. Legacy call vectors(path, w): all bits above 16 are fractional. vectors(path, 18, 16, 0, 0): an 18-bit headroom input into a 16-bit output."""
    ifb_ = (w - 16) if ifb is None else ifb
    scale = 1 << ifb_
    e = Engine(w, wo, ifb, ofb)
    wide_head = w - 16 - ifb_ > 0
    lines = []
    ns = 0

    def load(words):
        for i, v in enumerate(words):
            e.write(i, v)
            lines.append(f"W {i} {v}")

    def run(n, seed, bypass=0, mid=None, amp=1.0):
        nonlocal ns
        xs = stim(n, scale, seed, amp)
        for i, x in enumerate(xs):
            xl, xr = x, xs[(i + 7) % n]
            if mid is not None and i == 0:
                el, er = e.sample(xl, xr, bypass)
                e.commit(mid)
                lines.append(f"P {mid} {bypass} {xl} {xr} {el} {er}")
            else:
                el, er = e.sample(xl, xr, bypass)
                lines.append(f"S {bypass} {xl} {xr} {el} {er}")
            ns += 1

    A, B, C = bank_set(7, 1, -1.0), bank_set(17, 2, -3.0), bank_set(1, 3, 0.0)
    run(8, 9)                                   # nact 0: bypass
    load(A); e.commit(7); lines.append("C 7"); run(120, 10)
    load(B); run(40, 11)                        # shadow only: output must stay on A
    run(60, 12, mid=17)                         # commit mid-sample: B from the next sample
    run(120, 13)
    e.clear(); lines.append("Z"); run(60, 14)
    run(40, 15, bypass=1); run(40, 16)
    load(C); e.commit(1); lines.append("C 1"); run(60, 17)
    load(B); e.commit(17); lines.append("C 17"); run(60, 18)
    if wide_head:                               # hot input above 16-bit full scale (up to 1.6x): through B's -3 dB preamp it fits, in bypass it clamps at the output
        run(80, 19, amp=1.6); run(40, 20, bypass=1, amp=1.6)
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    return ns


if __name__ == "__main__":
    if "--vectors" in sys.argv:
        path = sys.argv[sys.argv.index("--vectors") + 1]
        w = int(sys.argv[sys.argv.index("--w") + 1]) if "--w" in sys.argv else 16
        opt = lambda k: int(sys.argv[sys.argv.index(k) + 1]) if k in sys.argv else None
        print(vectors(path, w, opt("--wo"), opt("--ifb"), opt("--ofb")), "samples")
    else:
        e = Engine(16)
        e.write(0, 1 << FC); e.write(1, 0); e.write(2, 0); e.write(3, 0); e.write(4, 0); e.write(PRE, 1 << FC)
        e.commit(1)
        assert e.sample(1234, -4321, 0) == (1234, -4321), "unity biquad must pass the sample unchanged"
        e.clear(); assert e.state[0][0] == [0, 0, 0, 0]
        print("halcyon_engine_model selftest OK")
