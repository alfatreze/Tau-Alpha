#!/usr/bin/env python3
"""How much does the EQ's coefficient precision matter for the Sound Shaping stages? (B-608; docs/features/CYMO_AUDIO_STACK_REVIEW.md)

The shipped EQ stores coefficients as 18-bit signed Q2.16 (tools/gen_eq_coeffs.py). A low-frequency shelf has its poles close to z = 1, so its
response is very sensitive to the last bits of the coefficients. This prints, per stage and gain: the worst response error against the unrounded design,
and for the low shelf how evenly the response moves per nominal 0.5 dB gain step (a slider needs steps that are uniform and monotonic), for the
shipped 18-bit grid and for finer grids (extra fractional bits on every coefficient). Host only; not part of make test-host.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import gen_eq_coeffs as g
import halcyon_model as m

S = g.QSCALE
def q_at(c, extra_bits):
    sc = S * (1 << extra_bits)
    return tuple(round(v * sc) / (1 << extra_bits) for v in c)       # back on the S scale so response_db can read it

def worst_err(kind, f, q, gd, extra):
    c = g.design(kind, f, q, gd)
    ideal = [tuple(v * S for v in c)]; qd = [q_at(tuple(c), extra)]
    qd = [tuple(v for v in qd[0])]
    return max(abs(g.response_db(qd, fr) - g.response_db(ideal, fr)) for fr in g.FREQS)

def steps(extra, fr=(50.0, 80.0, 120.0)):
    out = {}
    for f_ in fr:
        prev = None; d = []
        for i in range(-18, 19):
            c = g.design("lowshelf", 100.0, 0.7, i * 0.5)
            r = g.response_db([q_at(tuple(c), extra)], f_)
            if prev is not None: d.append(r - prev)
            prev = r
        out[f_] = (min(d), max(d), sum(1 for x in d if x <= 0))
    return out

if __name__ == "__main__":
    print("worst response error (dB) against the unrounded design, extra fractional bits on every coefficient (0 = shipped Q2.16)")
    print("%-10s %-7s %s" % ("stage", "gain", "  ".join("+%d bit" % e for e in (0, 2, 4, 6, 8))))
    for nm, k, f, qq in m.STAGES:
        for gd in (-9.0, -4.5, 4.5, 9.0):
            print("%-10s %+5.1f   %s" % (nm, gd, "  ".join("%7.3f" % worst_err(k, f, qq, gd, e) for e in (0, 2, 4, 6, 8))))
    print("\nlow shelf: response change per nominal 0.5 dB step (min, max, non-monotonic count), at 50/80/120 Hz")
    for e in (0, 2, 4, 6, 8):
        r = steps(e)
        print("  +%d bit: %s" % (e, "   ".join("%g Hz [%.2f, %.2f, %d]" % (f_, *v) for f_, v in r.items())))
