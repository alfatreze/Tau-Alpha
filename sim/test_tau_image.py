#!/usr/bin/env python3
"""Tests for tools/tau_image.py and sync_media.py --art-variants (skipped when Pillow/numpy are missing)."""
import json, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
try:
    import numpy as np
    from PIL import Image
except ImportError:
    print("SKIP test_tau_image: Pillow/numpy not installed")
    sys.exit(0)
import tau_image as ti

fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + (f"  {extra}" if extra and not ok else ""))
    fails += not ok

# smooth colour field + a little noise, 92x92 (not a multiple of 4 in the BC1 sense of 'size': 92 is, so also test 90)
yy, xx = np.mgrid[0:92, 0:92]
rng = np.random.default_rng(1)
rgb = np.clip(np.stack([128 + 100 * np.sin(xx / 9.0), 128 + 100 * np.sin(yy / 7.0 + 1),
                        128 + 100 * np.sin((xx + yy) / 11.0 + 2)], -1) + rng.normal(0, 3, (92, 92, 3)), 0, 255).astype(np.uint8)

# bit packing round trips at every width, awkward counts included
for bpp, top in ((8, 255), (6, 63), (4, 15)):
    for n in (1, 5, 8464, 8465):
        idx = rng.integers(0, top + 1, n).astype(np.uint8)
        check(f"pack {bpp} bpp n={n}", (ti.unpack_indices(ti.pack_indices(idx, bpp), bpp, n) == idx).all())

# exact sizes
h = 16
check("rgb565 size", len(ti.enc_rgb565(rgb)) == h + 92 * 92 * 2)
check("pal256 size", len(ti.encode(rgb, "pal256")) == h + 512 + 92 * 92)
check("pal64 size", len(ti.encode(rgb, "pal64")) == h + 128 + (92 * 92 * 6 + 7) // 8)
check("pal16 size", len(ti.encode(rgb, "pal16")) == h + 32 + (92 * 92 + 1) // 2)
check("bc1 size", len(ti.encode(rgb, "bc1")) == h + (92 // 4) ** 2 * 8)

# round trip quality floors (deliberately loose; they catch a broken codec, not a slightly worse one)
# the test field is fully saturated plus noise, a hard case for palettes; real covers score higher (docs/IMAGE_FORMATS.md)
floors = {"rgb565": 40, "pal256": 22, "pal64": 18, "pal16": 14, "bc1": 24, "jpg85": 26}
for v, fl in floors.items():
    d = ti.encode(rgb, v)
    s = ti.score(rgb, d)
    check(f"{v} psnr {s:.1f} >= {fl}", s >= fl)
check("rgb565 is lossless after quantise", ti.psnr(ti.quantise565(rgb), ti.decode(ti.enc_rgb565(rgb))) == 99.0)

# bc1: a hand-built block decodes by the standard rules (c0 > c1 -> four colours)
import struct
w0, w1 = 0xF800, 0x001F                                      # red, blue
blk = struct.pack("<HHI", w0, w1, 0b11100100_11100100_11100100_11100100 & 0xFFFFFFFF)
px = ti.dec_bc1(4, 4, 4, 0, blk)
check("bc1 hand block index0 red", tuple(px[0, 0]) == (255, 0, 0))
check("bc1 hand block index1 blue", tuple(px[0, 1]) == (0, 0, 255))
check("bc1 hand block index2 = 2/3 red + 1/3 blue", tuple(px[0, 2]) == ((2 * 255) // 3, 0, 255 // 3))

# odd sizes and header checks
odd = rgb[:90, :90]
d = ti.encode(odd, "bc1")
check("bc1 90x90 decodes to 90x90", ti.decode(d).shape == (90, 90, 3))
try:
    ti.decode(d[:-1]); check("truncated file rejected", False)
except ValueError:
    check("truncated file rejected", True)
check("estimates ordered lo<=hi", all(ti.estimate_ms(ti.encode(rgb, v))[0] <= ti.estimate_ms(ti.encode(rgb, v))[1] for v in ti.VARIANTS))
da, va, sa = ti.encode_auto(rgb)
check("auto picks a candidate", va in ti.AUTO_CANDIDATES)

# proportional scaling: long side = size, aspect kept, no padding
tall = Image.fromarray(np.zeros((1540, 1024, 3), np.uint8))
t = ti.fit_long_side(tall, 128)
check("portrait 1024x1540 -> 85x128", t.shape == (128, 85, 3), str(t.shape))
wide = ti.fit_long_side(Image.fromarray(np.zeros((100, 400, 3), np.uint8)), 128)
check("landscape 400x100 -> 128x32", wide.shape == (32, 128, 3), str(wide.shape))
ns = ti.encode(t, "pal256")
check("non-square palette round trip", ti.decode(ns).shape == (128, 85, 3))
check("non-square bc1 round trip", ti.decode(ti.encode(t, "bc1")).shape == (128, 85, 3))
check("default is pal256 at 128", ti.DEFAULT_VARIANTS == "pal256" and ti.DEFAULT_SIZE == 128)

# sync_media end to end on a fake card: variants land in <album>/tau-art, verified, carried by --from-core
with tempfile.TemporaryDirectory() as t:
    t = Path(t)
    card = t / "card"
    for core in ("alfatreze.TAU_A", "alfatreze.TAU_B"):
        (card / "Cores" / core).mkdir(parents=True)
        (card / "Cores" / core / "core.json").write_text(json.dumps({"core": {"metadata": {"platform_ids": [core.split(".")[1].lower()]}}}))
    (card / "Assets" / "tau_a" / "common").mkdir(parents=True)
    (card / "Assets" / "tau_b" / "common").mkdir(parents=True)
    alb = t / "Album"; alb.mkdir()
    (alb / "01 a.mp3").write_bytes(b"ID3\x03\x00\x00\x00\x00\x00\x00" + b"\0" * 64)
    Image.fromarray(rgb).save(alb / "cover.jpg", quality=90)
    sm = [sys.executable, str(ROOT / "tools" / "sync_media.py"), "--card", str(card)]
    r = subprocess.run(sm + [str(alb), "--core", "alfatreze.TAU_A", "--art-variants", "pal64,bc1,auto", "--art-size", "92", "--art-size", "64"],
                       capture_output=True, text=True)
    r0 = subprocess.run(sm + [str(alb), "--core", "alfatreze.TAU_B", "--art-variants"], capture_output=True, text=True)
    d0 = card / "Assets" / "tau_b" / "common" / "Album" / "tau-art" / "cover_128.pal256.timg"
    check("bare --art-variants writes cover_128.pal256.timg", r0.returncode == 0 and d0.exists(), r0.stdout[-300:] + r0.stderr[-300:])
    if d0.exists():
        d0.unlink()
    check("sync with art variants exits 0", r.returncode == 0, r.stdout[-400:] + r.stderr[-400:])
    art = card / "Assets" / "tau_a" / "common" / "Album" / "tau-art"
    names = sorted(p.name for p in art.glob("*.timg")) if art.exists() else []
    check("six variant files written", names == sorted(["cover_92.pal64.timg", "cover_92.bc1.timg", "cover_92.timg",
                                                        "cover_64.pal64.timg", "cover_64.bc1.timg", "cover_64.timg"]), str(names))
    if names:
        d = (art / "cover_92.pal64.timg").read_bytes()
        check("written file decodes", ti.decode(d).shape == (92, 92, 3))
    r = subprocess.run(sm + ["--from-core", "alfatreze.TAU_A", "--core", "alfatreze.TAU_B", "--art-variants", "pal64"], capture_output=True, text=True)
    check("clone with --art-variants exits 0", r.returncode == 0, r.stdout[-400:] + r.stderr[-400:])
    art_b = card / "Assets" / "tau_b" / "common" / "Album" / "tau-art"
    check("clone carries all six sidecars (no duplicates)", art_b.exists() and len(list(art_b.glob("*.timg"))) == 6,
          str(list(art_b.glob('*'))) if art_b.exists() else "no tau-art")
    r = subprocess.run(sm + [str(alb), "--core", "alfatreze.TAU_A", "--art-variants", "nope"], capture_output=True, text=True)
    check("unknown variant refused", r.returncode != 0)

print("test_tau_image:", "FAILED" if fails else "passed", f"({fails} failures)" if fails else "")
sys.exit(1 if fails else 0)
