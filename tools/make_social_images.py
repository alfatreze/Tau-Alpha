#!/usr/bin/env python3
"""Tau's social preview (1280x640) and README banner (2000x640), in the layout of Tau Gallery's pair (https://github.com/alfatreze/Tau-Gallery, docs/images) but set in Space Grotesk.

    python3 tools/make_social_images.py --shots a.png b.png c.png d.png [--version 0.7.5] [--out docs/images]

`--shots`: up to four Pocket screenshots (400x360 PNGs, Menu+Start on the Pocket), placed in the order given. Nothing is downloaded and no font is
fetched: the type is third_party/font/SpaceGrotesk-Bold.ttf and -SemiBold.ttf (OFL, third_party/font/OFL-SpaceGrotesk.txt). Deterministic: the same inputs give the
same bytes. The words and the colours are the constants below; change them here, not in the pictures.

Gallery's measured palette: background #0a0d14 (top left) to #121623 (bottom), a warm glow (#40363c to #433330) behind the pictures on the right, title #f4f6fa,
version and bullets #dd8b42, bullet text #dee6f0."""
import argparse, sys
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent
FONT_DIR = ROOT / "third_party/font"
BOLD, SEMI = FONT_DIR / "SpaceGrotesk-Bold.ttf", FONT_DIR / "SpaceGrotesk-SemiBold.ttf"

TITLE = ["TAU ALPHA", "MUSIC PLAYER"]
SUBTITLE = "for the Analogue Pocket"
BULLETS = ["Live meters: up to 64, build your own",
           "Every meter keeps its own settings",
           "MP3 and FLAC, library, album art",
           "Halcyon EQ, speed change, ReplayGain"]
FOOT = "Free and open source"
FOOT_URL = "github.com/alfatreze/Tau-Alpha"

C_TITLE, C_ORANGE, C_SUB, C_BULLET, C_FOOT = (244, 246, 250), (221, 139, 66), (172, 178, 192), (222, 230, 240), (124, 131, 148)


def font(path, size): return ImageFont.truetype(str(path), size)


def background(w, h):
    """dark navy, lighter toward the bottom, with a warm glow behind the pictures on the right (the colours measured on Gallery's image)"""
    y = np.linspace(0, 1, h)[:, None]; x = np.linspace(0, 1, w)[None, :]
    top, bot = np.array([10, 13, 20.0]), np.array([18, 22, 35.0])
    base = top[None, None, :] * (1 - y[..., None]) + bot[None, None, :] * y[..., None]
    base = base * np.ones((h, w, 1)) + (x[..., None] ** 2) * np.array([8, 7, 9.0])[None, None, :]
    glow = np.array([72, 54, 52.0])
    cx, cy = 0.80 * w, 0.52 * h
    d2 = ((np.arange(w)[None, :] - cx) / (0.30 * w)) ** 2 + ((np.arange(h)[:, None] - cy) / (0.62 * h)) ** 2
    g = np.exp(-d2)[..., None]
    out = base + (glow - base) * g * 0.95
    return Image.fromarray(np.clip(out, 0, 255).astype("uint8"), "RGB")


def framed(shot, size):
    """a Pocket screenshot scaled to `size` with a black bezel, a thin light edge, rounded corners and a soft shadow; returns an RGBA tile with room for the shadow"""
    pad = 22; w, h = size
    im = shot.convert("RGB").resize(size, Image.LANCZOS)
    tile = Image.new("RGBA", (w + 2 * pad, h + 2 * pad), (0, 0, 0, 0))
    sh = Image.new("RGBA", tile.size, (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle((pad, pad + 6, pad + w, pad + h + 6), 14, fill=(0, 0, 0, 150))
    tile.alpha_composite(sh.filter(ImageFilter.GaussianBlur(10)))
    bez = Image.new("RGBA", (w + 6, h + 6), (0, 0, 0, 0)); d = ImageDraw.Draw(bez)
    d.rounded_rectangle((0, 0, w + 5, h + 5), 14, fill=(0, 0, 0, 255), outline=(52, 60, 76, 255), width=2)
    tile.alpha_composite(bez, (pad - 3, pad - 3))
    mask = Image.new("L", (w, h), 0); ImageDraw.Draw(mask).rounded_rectangle((0, 0, w - 1, h - 1), 11, fill=255)
    tile.paste(im.convert("RGBA"), (pad, pad), mask)
    return tile, pad


def fit_text(draw, text, path, width, start):
    s = start
    while s > 20 and draw.textlength(text, font=font(path, s)) > width: s -= 2
    return font(path, s)


def left_column(img, version, banner):
    d = ImageDraw.Draw(img)
    x = 56 if not banner else 64
    ft = font(BOLD, 52 if not banner else 58)
    ty, tstep = (44, 56) if not banner else (38, 62)
    for i, line in enumerate(TITLE): d.text((x, ty + i * tstep), line, font=ft, fill=C_TITLE)
    vy = ty + 2 * tstep + 8
    fv = fit_text(d, version, BOLD, 400 if not banner else 440, 124 if not banner else 140)
    d.text((x - 2, vy), version, font=fv, fill=C_ORANGE)
    sy = vy + (150 if not banner else 168)
    d.text((x, sy), SUBTITLE, font=font(SEMI, 28), fill=C_SUB)
    y0, step, fs = (sy + 62, 32, 21) if not banner else (sy + 62, 36, 23)
    fb = font(SEMI, fs)
    for i, t in enumerate(BULLETS):
        y = y0 + i * step
        r = 6
        d.ellipse((x + 2, y + fs // 2 - r + 3, x + 2 + 2 * r, y + fs // 2 + r + 3), fill=C_ORANGE)
        d.text((x + 30, y), t, font=fb, fill=C_BULLET)
    foot = FOOT + (("   -   " + FOOT_URL) if banner else "")
    d.text((x, 590 if banner else 596), foot, font=font(SEMI, 20 if banner else 18), fill=C_FOOT)


def place(img, shots, spec):
    for shot, (x, y, w, h) in zip(shots, spec):
        tile, pad = framed(shot, (w, h))
        img.paste(tile, (x - pad, y - pad), tile)


def social(shots, version):
    img = background(1280, 640)
    left_column(img, version, False)
    spec = [(566, 40, 300, 270), (896, 64, 300, 270), (582, 336, 300, 270), (916, 356, 300, 270)]
    place(img, shots, spec)
    return img


def banner(shots, version):
    img = background(2000, 640)
    left_column(img, version, True)
    sz = (322, 290)
    spec = [(636, 196, *sz), (966, 150, *sz), (1296, 214, *sz), (1626, 156, *sz)]
    place(img, shots, spec)
    return img


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--shots", nargs="+", required=True, type=Path, help="1 to 4 Pocket screenshots (400x360 PNG)")
    ap.add_argument("--version", default="0.7.5")
    ap.add_argument("--out", type=Path, default=ROOT / "docs/images")
    a = ap.parse_args()
    shots = []
    for p in a.shots[:4]:
        im = Image.open(p)
        if im.size != (400, 360): sys.exit(f"{p}: {im.size}, a Pocket screenshot is 400x360")
        shots.append(im)
    while len(shots) < 4: shots.append(shots[len(shots) % len(a.shots[:4])])
    a.out.mkdir(parents=True, exist_ok=True)
    social(shots, a.version).save(a.out / "social-preview-1280x640.png", optimize=True)
    banner(shots, a.version).save(a.out / f"tau-alpha-{a.version}-banner.png", optimize=True)
    print("wrote", a.out / "social-preview-1280x640.png", "and", a.out / f"tau-alpha-{a.version}-banner.png")


if __name__ == "__main__":
    main()
