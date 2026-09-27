#!/usr/bin/env python3
"""Host test for the cover-source rules of sync_media.py --art-variants: FLAC PICTURE extraction (tools/tau_image.py) and the
last-resort image search (tools/sync_media.py find_any_image)."""
import sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import tau_image as ti
import sync_media as sm

fails = 0
def check(name, cond):
    global fails
    print(("ok   " if cond else "FAIL ") + name)
    fails += 0 if cond else 1

def u32(n): return n.to_bytes(4, "big")
def pic_block(img, ptype, last):
    body = u32(ptype) + u32(10) + b"image/jpeg" + u32(0) + b"" + u32(1) + u32(1) + u32(24) + u32(0) + u32(len(img)) + img
    return bytes([(0x80 if last else 0) | 6]) + len(body).to_bytes(3, "big") + body
def flac(blocks):
    si = bytes([0, 0, 0, 34]) + bytes(34)
    return b"fLaC" + si + b"".join(blocks) + b"\xff\xf8" + bytes(16)

JPG = b"\xff\xd8\xff\xe0" + b"J" * 40
with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    (td / "a.flac").write_bytes(flac([pic_block(b"\xff\xd8\xff\xe0" + b"B" * 20, 0, False), pic_block(JPG, 3, True)]))
    check("FLAC PICTURE: the front cover (type 3) wins over another picture", ti.extract_embedded(td / "a.flac") == JPG)
    (td / "b.flac").write_bytes(flac([pic_block(JPG, 0, True)]))
    check("FLAC PICTURE: a lone picture of another type is still used", ti.extract_embedded(td / "b.flac") == JPG)
    (td / "c.flac").write_bytes(flac([]))
    check("FLAC without a picture gives None", ti.extract_embedded(td / "c.flac") is None)
    (td / "d.flac").write_bytes(b"fLaC\x80\x00\x00\x02")
    check("a truncated FLAC does not crash", ti.extract_embedded(td / "d.flac") is None)

    f = td / "alb"; (f / "Artwork").mkdir(parents=True)
    check("no image at all gives None", sm.find_any_image(f) is None)
    (f / "Artwork" / "00 Booklet.jpg").write_bytes(b"x" * 500)
    (f / "Artwork" / "04 Case Front.jpg").write_bytes(b"x" * 100)
    (f / "Artwork" / "05 Disc.jpg").write_bytes(b"x" * 900)
    check("a name that says front beats a larger file", sm.find_any_image(f).name == "04 Case Front.jpg")
    (f / "Artwork" / "04 Case Front.jpg").unlink()
    check("with no preferred name the largest image wins", sm.find_any_image(f).name == "05 Disc.jpg")
    (f / "loose.png").write_bytes(b"x" * 10)
    check("(largest still wins across the folder and its sub-folder)", sm.find_any_image(f).name == "05 Disc.jpg")
    (f / "cover-x.jpg").write_bytes(b"x")
    check("a top-level cover-* name is preferred", sm.find_any_image(f).name == "cover-x.jpg")

print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
