#!/usr/bin/env python3
"""One-off extractor: the MPEG-1 Audio synthesis window D[i] (ISO/IEC 11172-3, Annex B, Table 3-B.3) from lieff/minimp3 (CC0 1.0), written to
tools/data/mp3_synthesis_window.txt. Run it again only to re-verify the data file (it must reproduce the file byte for byte):

    python3 tools/extract_mp3_window.py PATH/TO/minimp3.h [--commit SHA] [--write]

What it reads from minimp3.h (nothing else): the `g_win[]` table (15 rows x 16 integers = D[i]*65536 for every i with i mod 32 in 1..15, in minimp3's own
order) and the 16 integer literals of `mp3d_synth_pair` (the coefficients minimp3 keeps outside g_win: |D[32k]| for k = 1..8 and |D[16+32m]| for the 8 values of
the "sample 16" output). Everything else is derived here, from the structure of the window itself:
  * the prototype h[i] = (-1)^(i div 64) * D[i] is symmetric about i = 256 (so D[512-i] = -D[i] except at multiples of 64, where D[512-i] = D[i]);
    only D[0..256] are stored;
  * the 16 magnitudes minimp3 keeps in code carry no index and no sign for the window: each is placed at the index (and given the sign) where the prototype h
    is smoothest, i.e. where it is closest to the mean of its two neighbours (all neighbours are in g_win). The best placement must     factor of 1.5 or the run stops (the narrowest case, D[16] = -5 against D[144] = -45, is 79 against 158 in units of 1/65536). (sim/test_mp3_window_data.py shows the result equals Helix's table and the pl_mpeg/kjmp2 table.)
D[0] = 0 (the first entry of Table 3-B.3; minimp3 has no term for it).
"""
import argparse, hashlib, itertools, re, sys
from pathlib import Path

DATA = Path(__file__).resolve().parent / "data/mp3_synthesis_window.txt"


def parse(text):
    i = text.index("g_win[]")
    body = text[text.index("{", i) + 1:text.index("};", i)]
    g = [int(x) for x in re.findall(r"-?\d+", body)]
    assert len(g) == 240, len(g)
    j = text.index("mp3d_synth_pair(")
    fn = text[j:text.index("\n}\n", j)]
    lit = [int(x) for x in re.findall(r"\*\s*(-?\d+)\s*;", fn)]
    assert len(lit) == 16, lit
    return g, lit


def build(g, lit):
    D = {}
    for r in range(1, 16):
        for c in range(16):
            i = r + 16 * c if c % 2 == 0 else 512 - 16 * (c + 1) + r          # minimp3's column order inside a row
            D[i] = g[(r - 1) * 16 + c]
    # every value with i mod 32 in 1..15 is now known for i in 0..511; the neighbours of the 16 placed values are all among them
    def d(i):
        return D[i] if i in D else -D[512 - i]
    def h(i, v): return (-1) ** (i // 64) * v
    groups = [([32 * k for k in range(1, 9)], [abs(x) for x in lit[:8]]),            # |D[32k]|, k = 1..8
              ([16 + 32 * m for m in range(8)], [abs(x) for x in lit[8:]])]          # |D[16+32m]|, m = 0..7 (only the SET of magnitudes is used, not minimp3's order)
    placed = {0: 0}
    for slots, mags in groups:
        def cost(i, m):
            best = None
            for s in (1, -1):
                e = abs(h(i, s * m) - (h(i - 1, d(i - 1)) + h(i + 1, d(i + 1))) / 2)
                if best is None or e < best[0]: best = (e, s)
            return best
        results = []
        for perm in itertools.permutations(mags):
            tot = sum(cost(i, m)[0] for i, m in zip(slots, perm))
            results.append((tot, perm))
        results.sort()
        if not results[0][0] * 1.5 < results[1][0]: sys.exit("placement of the minimp3 literals is not decisive: %s vs %s" % (results[0], results[1]))
        for i, m in zip(slots, results[0][1]): placed[i] = cost(i, m)[1] * m
    full = {**D, **placed}
    out = {}
    for i in range(257):
        if i in full: out[i] = full[i]
        else: out[i] = -full[512 - i] if i % 64 else full[512 - i]
    assert len(out) == 257
    return [out[i] for i in range(257)]


def render(vals, commit, sha):
    o = ["# MPEG-1 Audio synthesis window D[i], ISO/IEC 11172-3 Annex B Table 3-B.3. Value = D[i] * 65536 (every entry is an integer). i = 0..256.",
         "# For 256 < i < 512: D[i] = -D[512-i], except at multiples of 64 where D[i] = D[512-i] (the prototype (-1)^(i div 64) D[i] is symmetric about 256).",
         "# Source: lieff/minimp3 minimp3.h (CC0 1.0 Universal), g_win[] and the integer literals of mp3d_synth_pair, extracted by tools/extract_mp3_window.py.",
         "# minimp3 commit %s, minimp3.h SHA-256 %s. Provenance and verification: docs/features/MP3_WINDOW_TABLE_PROVENANCE_2026-11-25.md." % (commit, sha),
         "# format: <i> <D[i]*65536>"]
    return "\n".join(o + ["%d %d" % (i, v) for i, v in enumerate(vals)]) + "\n"


if __name__ == "__main__":
    ap = argparse.ArgumentParser(); ap.add_argument("minimp3_h", type=Path); ap.add_argument("--commit", default="48e3a085f0e1a99446b34614e1e7f17f09dc2e82"); ap.add_argument("--write", action="store_true")
    a = ap.parse_args()
    raw = a.minimp3_h.read_bytes()
    g, lit = parse(raw.decode())
    txt = render(build(g, lit), a.commit, hashlib.sha256(raw).hexdigest())
    if a.write: DATA.write_text(txt); print("wrote", DATA)
    else:
        same = DATA.read_text() == txt
        print("data file reproduced byte for byte" if same else "DIFFERS from the data file"); sys.exit(0 if same else 1)
