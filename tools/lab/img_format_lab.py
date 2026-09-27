#!/usr/bin/env python3
"""Compare Tau image formats on real pictures: size, quality (PSNR) and estimated load time.

  img_format_lab.py cover.jpg other.png                      # 128 px, every variant
  img_format_lab.py covers/*.jpg --size 96 --size 128        # several thumbnail sizes
  img_format_lab.py cover.jpg --variants pal64,bc1,jpg75     # a subset
  img_format_lab.py cover.jpg --sheet out.png                # side-by-side sheet (original then each variant)
  img_format_lab.py cover.jpg --markdown                     # table ready to paste into docs/IMAGE_FORMATS.md

The codecs live in tools/tau_image.py; add a variant there and it shows up here. PSNR is against the resized
original quantised to RGB565 (what the screen can show). Load times are model estimates: see tau_image.py.
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import tau_image as ti


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("images", nargs="+")
    ap.add_argument("--size", type=int, action="append", help="long-side size in px (aspect kept) (repeatable, default 128)")
    ap.add_argument("--variants", default=",".join(ti.VARIANTS), help="comma list (default: all)")
    ap.add_argument("--sheet", help="write a comparison sheet PNG (first size only)")
    ap.add_argument("--markdown", action="store_true")
    a = ap.parse_args()
    from PIL import Image
    import numpy as np
    variants = a.variants.split(",")
    for path in a.images:
        for size in a.size or [ti.DEFAULT_SIZE]:
            rgb = ti.fit_long_side(Image.open(path), size)
            h_, w_ = rgb.shape[:2]
            title = f"{Path(path).name} -> {w_}x{h_}"
            print(("\n### " if a.markdown else "\n== ") + title)
            hdr = ("Variant", "Bytes", "Bits/px", "PSNR dB", "Load est.")
            if a.markdown:
                print("| " + " | ".join(hdr) + " |\n|" + "---|" * len(hdr))
            else:
                print("%-10s %8s %8s %8s   %s" % hdr)
            tiles = [rgb]
            for v in variants:
                d = ti.encode(rgb, v)
                m = ti.describe(rgb, d)
                row = (v, f"{m['bytes']:,}", f"{m['bytes'] * 8 / (w_ * h_):.2f}", f"{m['psnr']:.1f}",
                       f"{m['ms_lo']:.0f}-{m['ms_hi']:.0f} ms")
                print(("| " + " | ".join(row) + " |") if a.markdown else "%-10s %8s %8s %8s   %s" % row)
                tiles.append(ti.decode(d))
            d, v, s = ti.encode_auto(rgb)
            print(f"auto picks: {v} ({len(d):,} B, {s:.1f} dB)")
            if a.sheet and size == (a.size or [ti.DEFAULT_SIZE])[0]:
                out = a.sheet if len(a.images) == 1 else f"{Path(a.sheet).stem}_{Path(path).stem}{Path(a.sheet).suffix}"
                Image.fromarray(np.hstack(tiles)).resize((w_ * len(tiles) * 3, h_ * 3), Image.NEAREST).save(out)
                print("sheet:", out, "(original, " + ", ".join(variants) + ")")
    return 0


if __name__ == "__main__":
    sys.exit(main())
