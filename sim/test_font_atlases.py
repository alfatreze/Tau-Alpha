#!/usr/bin/env python3
"""tools/gen_font_atlases.py: the Tau font records (docs/features/FONT_SPEC.md 3.2). An independent parser reads the files on disk and checks the layout, the CRC, the Latin-1 ranges, tabular digits, advances,
that the atlas words decode to ink inside the cell (nothing in the top row or last column, the bottom row only for descenders), that every visible Latin-1 glyph has ink, and that the files equal a fresh render
(determinism). Mutants of the checker's own inputs: a flipped bit fails the CRC, a clipped glyph fails the cell check."""
import struct, subprocess, sys, zlib
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import gen_font_atlases as g
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    if not ok: fails += 1
def parse(b):
    if b[:4] != b"TFNT": return None
    ver, flags, crc = struct.unpack_from("<HHI", b, 4)
    body = b[12:]
    if ver != 1 or zlib.crc32(body) & 0xFFFFFFFF != crc: return None
    fam = body[:16].rstrip(b"\0").decode(); weight, cw, ch, nr, base, _ = struct.unpack_from("<HBBBBH", body, 16)
    off = 24; ranges = [struct.unpack_from("<HH", body, off + 4 * i) for i in range(nr)]; off += 4 * nr
    n = sum(b2 - a + 1 for a, b2 in ranges)
    adv = list(body[off:off + n]); off += n + (-n % 4)
    words = list(struct.unpack_from(f"<{n * 32}I", body, off)); off += 4 * n * 32
    if off != len(body): return None
    return dict(family=fam, weight=weight, cw=cw, ch=ch, base=base, ranges=ranges, adv=adv, words=words, n=n)
def cell(rec, k):                                   # glyph k as 16 rows of 16 4-bit values
    w = rec["words"][k * 32:(k + 1) * 32]
    return [[(w[y * 2 + x // 8] >> (4 * (x % 8))) & 15 for x in range(16)] for y in range(16)]
codes = g.codes()
files = sorted(g.OUT.glob("*.tfnt"))
check("four records on disk (Space Grotesk SemiBold and Bold, Tamzen Regular and Bold)", [f.name for f in files] == ["SpaceGrotesk-Bold.tfnt", "SpaceGrotesk-SemiBold.tfnt", "Tamzen8x16b.tfnt", "Tamzen8x16r.tfnt"])
for f in files:
    r = parse(f.read_bytes())
    check(f"{f.name}: parses (magic, version, CRC, exact length)", r is not None)
    if not r: continue
    check(f"{f.name}: family, weight, 16 x 16 cell, ranges 0x20..0x7E and 0xA0..0xFF, 191 glyphs", r["family"] in ("Space Grotesk", "Tamzen") and r["weight"] in (400, 600, 700) and (r["cw"], r["ch"]) == (16, 16) and r["ranges"] == [(0x20, 0x7E), (0xA0, 0xFF)] and r["n"] == 191)
    check(f"{f.name}: every advance is 2..16", all(2 <= a <= 16 for a in r["adv"]))
    d = [r["adv"][c - 0x20] for c in range(0x30, 0x3A)]
    check(f"{f.name}: the ten digits share one advance (tabular figures)", len(set(d)) == 1)
    if r["family"] == "Tamzen":
        check(f"{f.name}: monospace, every advance is 8, and every pixel is full or empty (no scaling, no grey)", set(r["adv"]) == {8} and all(((w >> (4 * i)) & 15) in (0, 15) for w in r["words"] for i in range(8)))
    bad = []
    for k, c in enumerate(codes):
        rows = cell(r, k); ink = any(v for row in rows for v in row)
        if c not in (0x20, 0xA0) and not ink: bad.append(("blank", hex(c)))
        pixel = r["family"] == "Tamzen"                      # a native 8 x 16 bitmap: the top and bottom rows are part of its design; the outline fonts must stay off the edges
        if any(rows[0]) and not pixel: bad.append(("top", hex(c)))
        if any(rows[y][15] for y in range(16)): bad.append(("right", hex(c)))
        if pixel and any(rows[y][x] for y in range(16) for x in range(8, 16)): bad.append(("wider than 8", hex(c)))
        if any(rows[15]) and chr(c) not in g.DESCENDERS and not pixel: bad.append(("bottom", hex(c)))
    check(f"{f.name}: every visible glyph has ink and none clips the cell ({bad[:3]})", not bad)
    check(f"{f.name}: the space and the no-break space are empty glyphs", all(not any(v for row in cell(r, k) for v in row) for k in (0, 95)))
mean = lambda r: sum(v for w in r["words"] for v in [(w >> (4 * i)) & 15 for i in range(8)]) / len(r["words"])
sb, bd = (parse((g.OUT / n).read_bytes()) for n in ("SpaceGrotesk-SemiBold.tfnt", "SpaceGrotesk-Bold.tfnt"))
check("Bold carries more ink than SemiBold (weights really differ)", mean(bd) > mean(sb) * 1.04)
tr, tb = (parse((g.OUT / n).read_bytes()) for n in ("Tamzen8x16r.tfnt", "Tamzen8x16b.tfnt"))
check("Tamzen Bold carries more ink than Tamzen Regular", mean(tb) > mean(tr) * 1.1)
import re as _re
_bdf = (ROOT / "third_party/font/Tamzen8x16r.bdf").read_text()
_hex = _re.search(r"ENCODING 65\n.*?BITMAP\n((?:[0-9A-F]{2}\n){16})ENDCHAR", _bdf, _re.S).group(1).split()
_want = [[(int(h, 16) >> (7 - x)) & 1 for x in range(8)] for h in _hex]
_got = [[1 if cell(tr, 0x41 - 0x20)[y][x] == 15 else 0 for x in range(8)] for y in range(16)]
check("Tamzen 'A' in the record is exactly the font's own 8 x 16 bitmap (read straight from the BDF, native baseline at row 12)", _want == _got and any(map(any, _want)))
# the files equal a fresh render
r = subprocess.run([sys.executable, str(ROOT / "tools/gen_font_atlases.py"), "--check"], capture_output=True, text=True)
check("the files on disk equal a fresh render (determinism)", r.returncode == 0)
# mutants of the checks
raw = bytearray((g.OUT / "SpaceGrotesk-SemiBold.tfnt").read_bytes()); raw[100] ^= 1
check("mutant: one flipped bit is refused by the CRC", parse(bytes(raw)) is None)
bad_rows = [[0] * 16 for _ in range(16)]; bad_rows[0][3] = 200
check("mutant: ink in the top row is caught by the cell check", bool(g.check_cell({0x41: bad_rows}, {0x41: 8})))
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
