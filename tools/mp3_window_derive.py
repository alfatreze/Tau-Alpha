#!/usr/bin/env python3
"""The ROM tables of the hardware MP3 window unit (src/fpga/core/tau_mp3_poly.sv) derived from the MPEG-1 Audio standard, with no Helix data.

Inputs: the synthesis window D[i] (tools/data/mp3_synthesis_window.txt, ISO/IEC 11172-3 Table 3-B.3) and three facts about the unit:
  (a) its datapath (tau_mp3_poly.sv): per macro x = 0..7 of a row r, a low word `lo` and a high word `hi`, two ROM coefficients c1 (index 2x) and c2 (2x+1);
      rows 1..15: acc1 += lo*c1 - hi*c2 (output sample r) and acc2 += lo*c2 + hi*c1 (output sample 32-r); row 0: acc1 += lo*c1 - hi*c2; row 16: acc1 += lo*c1;
  (b) the push order of the 32 words of one slot (firmware ABI): P0 = X[0], P1..P16 = X[16..31], P17..P31 = X[15..1], where X[m] = sum_k S[k] cos(m(2k+1)pi/64);
  (c) the macro order: macro x takes the even age 2x for `lo` (rows 0..15) and the odd age 2x+1 (row 16, which has no `lo`).
The standard (11172-3, synthesis): V[n] = X[16+n] (n = 0..63) per slot; out[j] = sum_{k=0..15} D[j+32k] * V_age k[ j + 32 (k mod 2) ].
Unfolding V with the cosine symmetries (cos((64-m)a) = -cos(m a) for odd multiples of pi/64, period 128) gives every V[n] as +-X[m] for m = 0..31 or 0 (V[16] = 0).
From (a) and the standard, the age of `hi` in each macro follows from requiring the shared c1/c2 to serve both accumulators (rows 1..15); rows 0 and 16 have only one output, so
there the macro order of (c) is simply continued. Scale: the ROM holds 4*D[i] (Helix's Q30 >> 12 format; the unit's fixed-point shifts are set by the RTL, not by the standard).
Output: taps {(row, tap): (age, P)} and the 264 ROM coefficients, identical to what sim/helix_poly_reference.py gets by running Helix (tested by sim/test_mp3_poly_derive.py).
"""
from pathlib import Path

DATA = Path(__file__).resolve().parent / "data/mp3_synthesis_window.txt"
SCALE = 4


def window(path=DATA):
    """D[0..511] as integers (D * 65536) from the data file, extended by the symmetry of the prototype (-1)^(i div 64) D[i] about i = 256."""
    half = {}
    for line in Path(path).read_text().splitlines():
        if line.startswith("#") or not line.strip(): continue
        i, v = line.split(); half[int(i)] = int(v)
    assert sorted(half) == list(range(257)), "the data file must hold D[0..256]"
    return [half[i] if i <= 256 else (half[512 - i] if i % 64 == 0 else -half[512 - i]) for i in range(512)]


def v_word(n):
    """V[n] = sign * X[m]  ->  (m, sign); m = None for the zero entry V[16]."""
    m = 16 + n
    if m == 32: return None, 0
    if m < 32: return m, 1
    if m < 64: return 64 - m, -1
    return m - 64, -1                      # m = 64 .. 79: cos((64+q)a) = -cos(q a)


def push_index(m):
    return 0 if m == 0 else (m - 15 if m >= 16 else 32 - m)


def derive(D):
    taps, coef = {}, [0] * 264
    for r in range(16):
        for x in range(8):
            lo_m, s_lo = v_word(r)                            # even ages read V[r]
            hi_m, s_hi = v_word(32 + r)                       # odd ages read V[32+r]
            p_lo, p_hi = push_index(lo_m), push_index(hi_m)
            k = 2 * x
            c1 = s_lo * D[r + 32 * k]                         # acc1 (out[r]) lo term
            if r == 0:
                a = 15 - 2 * x                                # single output: the order is the one rows 1..15 get from the consistency below
                c2 = -s_hi * D[32 * a]                        # acc1 hi term is -c2 * hi
            else:
                s_lo2 = v_word(32 - r)[1]                     # out[32-r]'s even ages read V[32-r]: the same word as `lo`
                s_hi2 = v_word(64 - r)[1]                     # and its odd ages V[64-r]: the same word as `hi`
                assert v_word(32 - r)[0] == lo_m and v_word(64 - r)[0] == hi_m
                c2 = s_lo2 * D[32 - r + 32 * k]               # acc2 lo term is +c2 * lo
                ok = [a for a in range(1, 16, 2)
                      if c2 == -s_hi * D[r + 32 * a]          # acc1 hi term:  hi * (-c2)  = D[r+32a] * V[32+r]
                      and c1 == s_hi2 * D[32 - r + 32 * a]]   # acc2 hi term:  hi * (+c1)  = D[32-r+32a] * V[64-r]
                assert len(ok) == 1, (r, x, ok)
                a = ok[0]
            taps[(r, x)] = (2 * x, p_lo); taps[(r, 8 + x)] = (a, p_hi)
            coef[16 * r + 2 * x], coef[16 * r + 2 * x + 1] = SCALE * c1, SCALE * c2
    for x in range(8):                                        # row 16: out[16] = sum over odd ages of D[16+32a] * V[48] ; V[48] = -X[0]
        a = 2 * x + 1
        m, s = v_word(48)
        assert m == 0
        taps[(16, x)] = (a, push_index(m))
        coef[256 + x] = SCALE * s * D[16 + 32 * a]
    return taps, coef


def tables(path=DATA):
    return derive(window(path))


if __name__ == "__main__":
    t, c = tables()
    print(len(t), "taps,", len(c), "coefficients")
