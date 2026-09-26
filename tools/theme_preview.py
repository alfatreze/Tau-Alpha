#!/usr/bin/env python3
"""Draw a 400x360 preview sheet per built-in theme and polarity with the firmware's own font and ramp maths, so a theme can be
judged before it goes on a Pocket (docs/THEME_SPEC.md step 0b). Uses the roles from themes/*.json and the same accent handling as
fw/player.c (th_accent_of, ui_grad_set/ui_grad_at). Output: one PNG per theme/polarity plus a 2x2-per-theme contact sheet.

    python3 tools/theme_preview.py --out DIR [--accent-index N]
"""
import argparse, struct, sys, zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_themes as gt          # noqa: E402
import ui_snapshot_renderer as R  # noqa: E402

PAL = [0x0843, 0xF79E, 0x2D40, 0xEEE0, 0x8925, 0xD925, 0xE68F, 0x5D6D, 0x4F5D, 0x9B9D,
       0xFFC0, 0xE68F, 0xD925, 0xE97D, 0x4F5D, 0x5D6D, 0x6B59, 0xC0E0, 0x8C63]


def ramp_fn(top, bot):
    def grad_at(y):
        thr, den = (1, 5, 3, 7)[y & 3], R.FB_H - 1
        y = min(y, den)
        rem, out = den - y, []
        tl, bl = [(top >> 11) & 31, (top >> 5) & 63, top & 31], [(bot >> 11) & 31, (bot >> 5) & 63, bot & 31]
        for a, b in zip(tl, bl):
            if a >= b:
                num = (a - b) * rem
                base = num // den + int((num % den) * 8 > thr * den)
                out.append(b + base)
            else:
                num = (b - a) * y
                base = num // den + int((num % den) * 8 > thr * den)
                out.append(a + base)
        return out[0] << 11 | out[1] << 5 | out[2]
    return grad_at


def sheet(theme, pol, accent_idx):
    d = theme[pol]
    S = lambda k: gt.snap(d[k])
    acc = gt.acc_eff(PAL[accent_idx], pol)
    top = gt.grad_top(acc, d["bg_luma"])
    R.grad_at = ramp_fn(top, S("bg_bottom"))
    f = R.Frame()
    for y in range(R.FB_H):
        f.rect(0, y, R.FB_W, 1, R.grad_at(y))
    W = R.FB_W
    chrome, surf, prim, sec, fnt = S("chrome"), S("surface"), S("text_primary"), S("text_secondary"), S("faint")
    f.rect(0, 0, W, 28, chrome)
    f.text(16, 7, f"THEME {theme['name']} {pol.upper()}", "TS_1X", acc, chrome, 250)
    f.text(W - 16 - R.text_width("PREVIEW"), 7, "PREVIEW", "TS_1X", sec, chrome, 80)
    # now-playing text block straight on the ramp
    f.text(16, 40, "TRACK TITLE ON THE RAMP", "TS_15X", prim, R.grad_at(40), 360)
    f.text(16, 62, "Artist name in secondary", "TS_1X", sec, R.grad_at(62), 360)
    f.text(16, 80, "filename-in-faint.mp3", "TS_1X", fnt, R.grad_at(80), 360)
    # meter: track + accent fill
    for i in range(24):
        h = 10 + (i * 37) % 60
        f.rect(16 + i * 15, 150 - 60, 11, 60, S("surface_track"))
        f.rect(16 + i * 15, 150 - h, 11, h, acc)
    # panel with rows
    f.rect(16, 165, 368, 96, surf)
    R.rounded_rect_on(f, 22, 170, 356, 26, 5, acc, surf)
    f.text(32, 176, "SELECTED ROW", "TS_1X", surf, acc, 200)
    f.text(32, 206, "Unselected row", "TS_1X", prim, surf, 200)
    f.text(32, 232, "Secondary value", "TS_1X", sec, surf, 200)
    f.rect(300, 205, 70, 12, S("pill"))
    f.text(306, 205, "PILL", "TS_1X", acc, S("pill"), 60)
    # status colours
    for i, k in enumerate(("ok", "warn", "danger")):
        f.rect(16 + i * 40, 272, 34, 12, S(k))
    f.text(140, 271, "ERROR TEXT", "TS_1X", S("error"), R.grad_at(271), 120)
    f.rect(260, 274, 124, 6, S("surface_track"))
    f.rect(260, 274, 70, 6, acc)
    f.rect(16, 294, 368, 3, S("fs_track"))
    f.rect(16, 294, 150, 3, S("fs_red"))
    # overlay-style content area sample + action bar
    f.rect(0, 304, W, 28, S("base"))
    f.text(16, 311, "BASE COLOUR (UNDER OVERLAYS)", "TS_1X", prim, S("base"), 360)
    f.rect(0, R.FB_H - 28, W, 28, chrome)
    f.text(16, R.FB_H - 21, "A OPEN   B BACK   START CLOSE", "TS_1X", fnt if False else sec, chrome, 360)
    return f


def write_png(path, pixels, w, h):
    raw = b"".join(b"\x00" + b"".join(bytes(p) for p in pixels[y * w:(y + 1) * w]) for y in range(h))
    def chunk(t, data):
        c = struct.pack(">I", len(data)) + t + data
        return c + struct.pack(">I", zlib.crc32(t + data) & 0xFFFFFFFF)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--accent-index", type=int, default=1)
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    for t in gt.load():
        frames = [sheet(t, p, a.accent_index) for p in gt.POLS]
        W, H = R.FB_W, R.FB_H
        combo = []
        for y in range(H):
            for fr in frames:
                combo += fr.png_pixels()[y * W:(y + 1) * W]
        write_png(a.out / f"theme-{t['name'].lower()}.png", combo, W * 2, H)
        print("wrote", a.out / f"theme-{t['name'].lower()}.png", "(dark left, light right)")


if __name__ == "__main__":
    main()
