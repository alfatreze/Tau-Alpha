#!/usr/bin/env python3
"""Derive the glyph-coverage -> blend-weight table for text, from first principles (MIT, Tau Alpha).

Written new on 2026-11-25 for the Talos v2 rewrite (decision D-T20: the inherited 16-number table is redone).
Nothing here reads, copies or adapts any earlier table-fitting tool.

THE PROBLEM. A glyph pixel carries a 4-bit coverage c in 0..15: the fraction a = c/15 of the pixel the letter covers.
The engine blends in gamma-encoded RGB565 with an integer weight w in 0..16 (weight = w/16 of the foreground):
    out = bg + (fg - bg) * w / 16          (per channel, in encoded values)
THE PHYSICS. A pixel that is covered by a fraction a emits that fraction of the foreground's LIGHT: the mix is linear
in linear light, not in encoded values:
    out_linear = a * fg_linear + (1 - a) * bg_linear ,   out = encode(out_linear)
So the ideal weight in encoded space, for one (fg, bg) pair and one channel, is
    w_ideal(a) = (encode(a*L(fg) + (1-a)*L(bg)) - encode(bg)) / (encode(fg) - encode(bg))
(for light text on a dark ground this is larger than a, which is why plain linear alpha makes light-on-dark text look thin).
THE FIT. One table must serve every text colour on every background of the real themes. For each c we pick the integer
w in 0..16 that minimises the weighted sum over all (fg, bg) pairs of the squared error in CIE L* between the ideal
pixel and the pixel the engine would produce (quantised to RGB565). Constraints: w(0) = 0 and w(15) = 16 (an untouched
pixel IS the background, a solid pixel IS the foreground), and the table is non-decreasing.

Usage:
  python3 tools/derive_text_weights.py                       # the table for the dark themes, with the error report
  python3 tools/derive_text_weights.py --polarity light      # dark text on a light ramp
  python3 tools/derive_text_weights.py --verilog             # print the table as a SystemVerilog case body
  python3 tools/derive_text_weights.py --preview out.png     # render sample text with each table for comparison
"""
import argparse
import glob
import json
import math
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
THEMES = os.path.join(ROOT, "themes", "*.json")
TEXT_ROLES = {"text_primary": 4.0, "text_secondary": 2.0, "faint": 1.0}      # role -> weight in the fit
BG_ROLES = {"bg_bottom": 3.0, "surface": 3.0, "surface_track": 1.0, "base": 1.0, "chrome": 1.0, "pill": 1.0}
LEVELS = 16          # weights 0..16
COVERAGE = 15        # coverage 0..15


# ---- colour maths --------------------------------------------------------------------------------------------------
def srgb_decode(v8):                      # 0..255 -> linear 0..1
    v = v8 / 255.0
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def srgb_encode(lin):                     # linear 0..1 -> 0..255 (float)
    lin = min(1.0, max(0.0, lin))
    v = lin * 12.92 if lin <= 0.0031308 else 1.055 * lin ** (1 / 2.4) - 0.055
    return 255.0 * v


def lstar(rgb8):
    y = 0.2126 * srgb_decode(rgb8[0]) + 0.7152 * srgb_decode(rgb8[1]) + 0.0722 * srgb_decode(rgb8[2])
    return 116.0 * y ** (1 / 3) - 16.0 if y > 216 / 24389 else 24389 / 27 * y


def parse_color(s):
    """'#RRGGBB' or '0xRGB565' -> (r, g, b) 8-bit as the panel would show them (565 expanded by bit replication)."""
    s = s.strip()
    if s.startswith("#"):
        r, g, b = int(s[1:3], 16), int(s[3:5], 16), int(s[5:7], 16)
        return unpack565(pack565((r, g, b)))
    return unpack565(int(s, 16))


def pack565(rgb):
    return ((rgb[0] >> 3) << 11) | ((rgb[1] >> 2) << 5) | (rgb[2] >> 3)


def unpack565(p):
    r, g, b = (p >> 11) & 31, (p >> 5) & 63, p & 31
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def blend565(bg, fg, w):
    """What the engine produces: a per-channel weighted mix in encoded values, truncated into RGB565 fields."""
    r = (bg[0] * (LEVELS - w) + fg[0] * w) // LEVELS
    g = (bg[1] * (LEVELS - w) + fg[1] * w) // LEVELS
    b = (bg[2] * (LEVELS - w) + fg[2] * w) // LEVELS
    return unpack565(pack565((r, g, b)))


def ideal_pixel(bg, fg, a):
    return tuple(round(srgb_encode(a * srgb_decode(f) + (1 - a) * srgb_decode(b))) for b, f in zip(bg, fg))


# ---- the palette pairs -----------------------------------------------------------------------------------------------
def theme_pairs(polarity):
    """[(fg, bg, weight)] for light text on dark grounds (polarity 'dark') or dark on light ('light')."""
    pairs = []
    for f in sorted(glob.glob(THEMES)):
        t = json.load(open(f))
        roles = t.get(polarity)
        if not roles:
            continue
        for tr, tw in TEXT_ROLES.items():
            if tr not in roles:
                continue
            for br, bw in BG_ROLES.items():
                if br in roles:
                    fg, bg = parse_color(roles[tr]), parse_color(roles[br])
                    if lstar(fg) != lstar(bg):
                        pairs.append((fg, bg, tw * bw))
    return pairs


# ---- the fit -----------------------------------------------------------------------------------------------------------
def pair_error(bg, fg, a, w):
    return (lstar(blend565(bg, fg, w)) - lstar(ideal_pixel(bg, fg, a))) ** 2


def derive(pairs):
    """-> (table of 16 ints, per-c total weighted error). Exact ends, non-decreasing."""
    table, errs = [], []
    for c in range(COVERAGE + 1):
        a = c / COVERAGE
        if c == 0:
            table.append(0)
            errs.append(0.0)
            continue
        if c == COVERAGE:
            table.append(LEVELS)
            errs.append(0.0)
            continue
        best = min(range(LEVELS + 1), key=lambda w: sum(wt * pair_error(bg, fg, a, w) for fg, bg, wt in pairs))
        table.append(best)
        errs.append(sum(wt * pair_error(bg, fg, a, best) for fg, bg, wt in pairs))
    for i in range(1, len(table)):                        # non-decreasing
        table[i] = max(table[i], table[i - 1])
    return table, errs


def linear_table():
    return [round(LEVELS * c / COVERAGE) for c in range(COVERAGE + 1)]


def report(table, pairs):
    tot = wsum = worst = 0.0
    for c in range(1, COVERAGE):
        a = c / COVERAGE
        for fg, bg, wt in pairs:
            e = abs(lstar(blend565(bg, fg, table[c])) - lstar(ideal_pixel(bg, fg, a)))
            tot += wt * e
            wsum += wt
            worst = max(worst, e)
    return tot / wsum, worst


# ---- preview -----------------------------------------------------------------------------------------------------------
def preview(path, polarity, tables):
    from PIL import Image, ImageDraw, ImageFont
    font_path = os.path.join(ROOT, "third_party", "font", "SpaceGrotesk-SemiBold.ttf")
    sample = [("Now Playing  Aa Gg 0123", 22), ("Settings, Library and meters", 16), ("ReplayGain  -6.2 dB  Halcyon EQ", 12)]
    theme = json.load(open(os.path.join(ROOT, "themes", "tau.json")))[polarity]
    fg = parse_color(theme["text_primary"])
    bg = parse_color(theme["surface"] if "surface" in theme else theme["bg_bottom"])
    cols = [("ideal (linear light, full coverage)", None)] + tables
    scale, pad, label_h = 5, 6, 14
    rows_h = [(size + 6) for _, size in sample]
    width = max(300, 340)
    img = Image.new("RGB", ((width + pad) * len(cols) * 1, (sum(rows_h) + label_h + pad) * 1), (30, 30, 30))
    big = Image.new("RGB", (width * len(cols), sum(rows_h) + label_h), (0, 0, 0))
    for ci, (name, table) in enumerate(cols):
        y = label_h
        for text, size in sample:
            f = ImageFont.truetype(font_path, size)
            mask = Image.new("L", (width, size + 6), 0)
            ImageDraw.Draw(mask).text((4, 1), text, font=f, fill=255)
            out = Image.new("RGB", mask.size, bg)
            px, mp = out.load(), mask.load()
            for yy in range(mask.size[1]):
                for xx in range(mask.size[0]):
                    m = mp[xx, yy]
                    if table is None:
                        px[xx, yy] = ideal_pixel(bg, fg, m / 255.0)
                    else:
                        c = round(m * COVERAGE / 255.0)             # the 4-bit coverage the engine sees
                        px[xx, yy] = blend565(bg, fg, table[c])
            big.paste(out, (ci * width, y))
            y += size + 6
    big = big.resize((big.size[0] * scale // 2, big.size[1] * scale // 2), Image.NEAREST)
    d = ImageDraw.Draw(big)
    for ci, (name, table) in enumerate(cols):
        d.text((ci * width * scale // 2 + 4, 2), name, fill=(255, 220, 120))
    big.save(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--polarity", choices=["dark", "light"], default="dark")
    ap.add_argument("--verilog", action="store_true")
    ap.add_argument("--preview", metavar="PNG")
    ap.add_argument("--compare", metavar="N,N,...", help="a 16-number table to report against (for example the current one)")
    a = ap.parse_args()
    pairs = theme_pairs(a.polarity)
    if not pairs:
        sys.exit("no (text, background) pairs found in themes/*.json for polarity " + a.polarity)
    table, errs = derive(pairs)
    print(f"polarity {a.polarity}: {len(pairs)} weighted (text, ground) pairs from {len(glob.glob(THEMES))} themes")
    print("derived table  :", table)
    tables = [("derived (new)", table), ("linear alpha", linear_table())]
    if a.compare:
        tables.insert(1, ("current (old)", [int(x) for x in a.compare.split(",")]))
    for name, t in tables:
        mean, worst = report(t, pairs)
        print(f"  {name:14} mean |dL*| {mean:5.2f}   worst |dL*| {worst:5.2f}   {t}")
    if a.verilog:
        print("\n// derived by tools/derive_text_weights.py (polarity %s)" % a.polarity)
        for c, w in enumerate(table):
            print("    4'd%-2d: cov_weight = 5'd%d;" % (c, w))
    if a.preview:
        preview(a.preview, a.polarity, tables)
        print("preview written:", a.preview)


if __name__ == "__main__":
    main()
