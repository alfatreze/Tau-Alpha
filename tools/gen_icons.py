#!/usr/bin/env python3
"""Player icons as a Tau icon record (docs/features/FONT_SPEC.md section 13).

Source: assets/icons/player_icons_strip.png, one row of 28-pixel-tall greyscale icons on black (the owner's pixel-style set: play, pause, back, repeat, shuffle on and off, speaker levels, ...). Icons in the strip are found by their
columns (the SPANS table: neighbouring icons are only 4 to 6 columns apart, no wider than the gaps inside the pause, speaker and wave icons). Each icon is stored as a 4-bit coverage bitmap (the grey level scaled to 0..15 and
rounded), packed two pixels to a byte, low nibble first, rows top to bottom with no padding between rows except the odd last nibble of a row. The firmware draws one with the palette blit (OP_CBLIT): a 16-entry colour table
is loaded at draw time, entry c = the background colour blended toward the foreground colour by c/15, so the icon follows the theme (the role map of FONT_SPEC section 13 is the choice of foreground and background roles at draw time).

Record (little endian): "TICN" | version u16 = 1 | flags u16 = 0 | crc32 u32 of everything after it | count u8 | reserved u8 x3 |
  count x { id u8, w u8, h u8, namelen u8, name[12], datalen u16, data[datalen] }

Usage: gen_icons.py [--check] [--preview]   (no flag: write fw2/generated/icons/player.ticn)
"""
import struct, sys, zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "assets" / "icons" / "player_icons_strip.png"
OUT = ROOT / "fw2" / "generated" / "icons" / "player.ticn"
PREVIEW = ROOT / "docs" / "features" / "font_preview" / "icons.png"
# The fourteen icons of the strip, left to right. Names are the owner's to confirm (docs/features/FONT_SPEC.md 13): the ids are what firmware uses.
NAMES = ("play", "pause", "back", "play_box", "repeat", "repeat_one", "shuffle_off", "shuffle", "note", "mute", "vol0", "vol1", "vol2", "vol3")


def load():
    from PIL import Image
    im = Image.open(SRC).convert("RGBA")
    bg = Image.new("RGBA", im.size, (0, 0, 0, 255)); bg.alpha_composite(im)
    g = bg.convert("L")
    return g


# The column span of each icon in the strip, found by looking at it (the gaps between neighbouring icons, 4 to 6 columns, are no wider than the gaps inside the pause, speaker and wave icons, so
# a gap rule cannot separate them). split() checks that the spans cover every inked column and nothing else.
SPANS = ((8, 19), (32, 48), (60, 79), (87, 108), (115, 136), (143, 164), (170, 193), (198, 221), (226, 247), (255, 276), (288, 299), (313, 330), (339, 360), (365, 390))


def split(g):
    w, h = g.size; px = g.load()
    inked = [x for x in range(w) if any(px[x, y] > 8 for y in range(h))]
    covered = {x for a, b in SPANS for x in range(a, b + 1)}
    stray = [x for x in inked if x not in covered]
    if stray: raise SystemExit(f"inked columns outside every icon span: {stray[:8]}")
    return list(SPANS)


def build():
    g = load(); px = g.load(); w, h = g.size
    top = max(px[x, y] for x in range(w) for y in range(h))
    spans = split(g)
    if len(spans) != len(NAMES): raise SystemExit(f"found {len(spans)} icons in the strip, expected {len(NAMES)}")
    icons = []
    for i, (a, b) in enumerate(spans):
        iw = b - a + 1
        cov = [[min(15, (px[a + x, y] * 15 + top // 2) // top) for x in range(iw)] for y in range(h)]
        data = bytearray()
        for row in cov:
            for x in range(0, iw, 2):
                data.append(row[x] | ((row[x + 1] << 4) if x + 1 < iw else 0))
        icons.append((i, iw, h, NAMES[i], bytes(data), cov))
    return icons


def record(icons):
    body = bytes([len(icons), 0, 0, 0])
    for i, w, h, name, data, _ in icons:
        nm = name.encode("ascii").ljust(12, b"\0")[:12]
        body += struct.pack("<BBBB", i, w, h, len(name)) + nm + struct.pack("<H", len(data)) + data
    return b"TICN" + struct.pack("<HHI", 1, 0, zlib.crc32(body) & 0xFFFFFFFF) + body


def preview(icons):
    from PIL import Image
    pad = 8; wtot = sum(w for _, w, _, _, _, _ in icons) + pad * (len(icons) + 1); h = icons[0][2]
    img = Image.new("L", (wtot, h + 2 * pad), 0); x = pad
    for _, w, hh, _, _, cov in icons:
        cell = Image.new("L", (w, hh)); cell.putdata([c * 17 for row in cov for c in row]); img.paste(cell, (x, pad)); x += w + pad
    return img.resize((img.width * 3, img.height * 3), Image.NEAREST)


def main():
    icons = build(); rec = record(icons)
    if "--preview" in sys.argv:
        PREVIEW.parent.mkdir(parents=True, exist_ok=True); preview(icons).save(PREVIEW); print("wrote", PREVIEW.relative_to(ROOT))
    if "--check" in sys.argv:
        same = OUT.exists() and OUT.read_bytes() == rec
        print(("ok   " if same else "STALE ") + str(OUT.relative_to(ROOT))); return 0 if same else 1
    OUT.parent.mkdir(parents=True, exist_ok=True); OUT.write_bytes(rec)
    print("wrote", OUT.relative_to(ROOT), len(rec), "bytes,", len(icons), "icons:", ", ".join(f"{n} {w}x{h}" for _, w, h, n, _, _ in icons))
    return 0


if __name__ == "__main__":
    sys.exit(main())
