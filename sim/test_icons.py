#!/usr/bin/env python3
"""Player icons (tools/gen_icons.py, docs/features/FONT_SPEC.md 13): an independent reader parses fw2/generated/icons/player.ticn and checks the layout and CRC, the fourteen names, sizes inside the engine's limits
(width at most 127, height 28), and that every icon equals the source strip's pixels (grey level scaled to 4 bits, read straight from assets/icons/player_icons_strip.png with its own column spans); the strip has no ink
outside the icons; the file equals a fresh render. Mutants: a flipped bit fails the CRC; a nibble swapped in the packing is caught by the pixel comparison."""
import struct, subprocess, sys, zlib
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(extra)))
    if not ok: fails += 1
def parse(b):
    if b[:4] != b"TICN": return None
    ver, flags, crc = struct.unpack_from("<HHI", b, 4); body = b[12:]
    if ver != 1 or zlib.crc32(body) & 0xFFFFFFFF != crc: return None
    n = body[0]; off = 4; icons = []
    for _ in range(n):
        i, w, h, nl = struct.unpack_from("<BBBB", body, off); name = body[off + 4:off + 4 + nl].decode(); dl = struct.unpack_from("<H", body, off + 16)[0]
        data = body[off + 18:off + 18 + dl]; off += 18 + dl
        if dl != h * ((w + 1) // 2): return None
        rows = [[(data[y * ((w + 1) // 2) + x // 2] >> (4 * (x % 2))) & 15 for x in range(w)] for y in range(h)]
        icons.append((i, name, w, h, rows))
    return icons if off == len(body) else None
rec = (ROOT / "fw2/generated/icons/player.ticn").read_bytes()
icons = parse(rec)
check("the record parses (magic, version, CRC, exact length, row sizes)", icons is not None)
NAMES = ("play", "pause", "back", "play_box", "repeat", "repeat_one", "shuffle_off", "shuffle", "note", "mute", "vol0", "vol1", "vol2", "vol3")
check("fourteen icons with the agreed names and ids 0..13", icons is not None and [(i, n) for i, n, *_ in icons] == list(enumerate(NAMES)))
check("every icon is at most 127 wide (the engine's row buffer) and exactly 28 tall", all(w <= 127 and h == 28 for _, _, w, h, _ in icons))
check("every pixel is a 4-bit coverage and each icon has ink", all(0 <= v <= 15 for *_, rows in icons for r in rows for v in r) and all(any(any(r) for r in rows) for *_, rows in icons))
from PIL import Image
im = Image.open(ROOT / "assets/icons/player_icons_strip.png").convert("RGBA"); bg = Image.new("RGBA", im.size, (0, 0, 0, 255)); bg.alpha_composite(im); g = bg.convert("L"); px = g.load()
top = max(px[x, y] for x in range(g.width) for y in range(g.height))
SP = ((8, 19), (32, 48), (60, 79), (87, 108), (115, 136), (143, 164), (170, 193), (198, 221), (226, 247), (255, 276), (288, 299), (313, 330), (339, 360), (365, 390))
ok = True
for (i, name, w, h, rows), (a, b) in zip(icons, SP):
    if w != b - a + 1: ok = False
    for y in range(h):
        for x in range(w):
            if rows[y][x] != min(15, (px[a + x, y] * 15 + top // 2) // top): ok = False
check("every icon equals the source strip's pixels (grey level scaled to 4 bits, rounded)", ok)
check("no inked column of the strip lies outside the icons", all(not any(px[x, y] > 8 for y in range(g.height)) for x in range(g.width) if not any(a <= x <= b for a, b in SP)))
r = subprocess.run([sys.executable, str(ROOT / "tools/gen_icons.py"), "--check"], capture_output=True, text=True)
check("the file equals a fresh render", r.returncode == 0)
raw = bytearray(rec); raw[40] ^= 1
check("mutant: one flipped bit is refused by the CRC", parse(bytes(raw)) is None)
sw = bytearray(rec); k = next(j for j in range(12 + 4 + 18, len(sw)) if (sw[j] & 15) != (sw[j] >> 4)); sw[k] = ((sw[k] & 15) << 4) | (sw[k] >> 4)       # the first byte of the first icon's data whose two nibbles differ
body = bytes(sw[12:]); sw[8:12] = struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF)
sw_icons = parse(bytes(sw))
check("mutant: swapped nibbles (a re-signed file) differ from the source pixels", sw_icons is not None and sw_icons[0][4] != icons[0][4])
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
