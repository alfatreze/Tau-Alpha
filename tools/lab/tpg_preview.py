#!/usr/bin/env python3
"""Mock-up of the report pages' grid views (fw/suite.inc rep_draw): same block geometry (tools/tpg.py), frame, caption lines and the
rule that a line touching the block is dropped. Text is drawn with a stand-in font, so only positions and wording are meaningful.

  tpg_preview.py out.png [--bytes 231] [--view robust|lossless] [--title "USER CHECK"] [--detail "RUN 3"]
"""
import argparse, os, sys
import numpy as np
from PIL import Image, ImageDraw
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import tpg

W, H = tpg.W, tpg.H
BG, DIM, FAINT, ACC = (8, 12, 16), (150, 155, 160), (90, 95, 100), (120, 200, 255)


def render(nbytes, view, title, detail, seed=5):
    payload = bytes(np.random.default_rng(seed).integers(0, 256, nbytes, dtype=np.uint8))
    mode, fell = (tpg.MODE_L if view == "lossless" else tpg.MODE_R), False
    if tpg.block(nbytes, mode) is None and mode == tpg.MODE_R:
        mode, fell = tpg.MODE_L, True
    img = Image.new("RGB", (W, H), BG)
    blk = tpg.block(nbytes, mode)
    if blk is None:
        raise SystemExit("too big for any grid")
    x0, y0, px = blk
    arr = np.asarray(img).copy()
    arr[y0:y0 + px, x0:x0 + px] = tpg.to_rgb(tpg.encode(payload, mode))[y0:y0 + px, x0:x0 + px]
    img = Image.fromarray(arr)
    d = ImageDraw.Draw(img)
    if x0 >= 8 and y0 >= 8:
        for r in ((x0 - 4, y0 - 4, x0 + px + 4, y0 - 3), (x0 - 4, y0 + px + 2, x0 + px + 4, y0 + px + 3),
                  (x0 - 4, y0 - 4, x0 - 3, y0 + px + 3), (x0 + px + 2, y0 - 4, x0 + px + 3, y0 + px + 3)):
            d.rectangle(r, fill=DIM)
    name = "ROBUST GRID" if mode == tpg.MODE_R else "LOSSLESS GRID"
    viewline = f"{name}  {nbytes} B" + ("  (TOO BIG FOR ROBUST)" if fell else "")
    def line(y, color, txt):
        if (y + 14 <= y0 - 4) or (y >= y0 + px + 4):
            d.text((24, y), txt, fill=color)
    line(8, ACC, title); line(22, DIM, viewline); line(36, DIM, detail)
    line(H - 36, FAINT, "X VIEW   B BACK"); line(H - 22, FAINT, "MENU+START FOR A SCREENSHOT")
    return img


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("out"); ap.add_argument("--bytes", type=int, default=231)
    ap.add_argument("--view", default="robust", choices=["robust", "lossless"]); ap.add_argument("--title", default="USER CHECK")
    ap.add_argument("--detail", default="RUN 3")
    a = ap.parse_args()
    render(a.bytes, a.view, a.title, a.detail).save(a.out)


if __name__ == "__main__":
    main()
