#!/usr/bin/env python3
"""tools/make_synthetic_meter_bin.py: the 64-row stress bins. Checked with the independent reader tools/meter_bin.py (every pack header against its row, the whole-file CRC): 64 rows, the seven real meters first, unique ids,
names TEST 08..TEST 64, a synthetic pack = a real pack with only its header meter id patched (image and CRC equal), the hang variant has HANG TEST at row 8 and is otherwise equal, --collide makes rows 9 and 10 share their low 16 id bits,
the output is deterministic, fewer than 8 or more than 64 rows are refused."""
import sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools")); sys.path.insert(0, str(ROOT / "tools/player2"))
import meter_bin as mb, make_synthetic_meter_bin as sy
fails = 0
def check(n, ok, i=""):
    global fails
    print(("ok   " if ok else "FAIL ") + n + ("  " + str(i)[:300] if not ok else "")); fails += 0 if ok else 1
with tempfile.TemporaryDirectory() as td:
    out = Path(td) / "o"; sy.main([str(out)])
    a = (out / "tau-meters.bin").read_bytes(); h = (out / "tau-meters-hang.bin").read_bytes()
    for nm, b in (("plain", a), ("hang", h)):
        try: mb.check_file(b); ok = True
        except mb.BinError as e: ok = False; print(e)
        check("the %s bin passes the independent reader (whole-file CRC, TOC, every pack against its row)" % nm, ok)
    ra = mb.open_toc(a)["rows"]; rh = mb.open_toc(h)["rows"]
    check("64 rows, the seven shipped meters first (Winamp Bars the fallback), then TEST 08 ... TEST 64", len(ra) == 64 and ra[0]["name"] == "WINAMP BARS" and [r["name"] for r in ra[7:]] == ["TEST %02d" % n for n in range(8, 65)], [r["name"] for r in ra[:9]])
    check("ids are unique, also in id_hash and in the low 16 bits", len({r["id"] for r in ra}) == 64 and len({r["id"][:8] for r in ra}) == 64 and len({r["id"][4:8] for r in ra}) == 64)
    check("the hang variant differs only at row 8 (HANG TEST); every other row's name and id are the plain bin's", rh[7]["name"] == "HANG TEST" and [(r["id"], r["name"]) for i, r in enumerate(rh) if i != 7] == [(r["id"], r["name"]) for i, r in enumerate(ra) if i != 7])
    p8 = a[ra[8]["pack_off"]:ra[8]["pack_off"] + ra[8]["pack_size"]]; src = None
    for r in ra[1:7]:
        p = a[r["pack_off"]:r["pack_off"] + r["pack_size"]]
        if p[8:] == p8[8:] or (p[:6] == p8[:6] and p[8:] == p8[8:]): src = p
    check("a synthetic pack is a real pack with only the header's meter id changed (the image, its CRC and descriptor are equal)", src is not None and p8[6:8] == bytes([int(ra[8]["id"][:8], 16) & 255, (int(ra[8]["id"][:8], 16) >> 8) & 255]))
    out2 = Path(td) / "o2"; sy.main([str(out2)])
    check("deterministic: a second run writes the same bytes", (out2 / "tau-meters.bin").read_bytes() == a)
    out3 = Path(td) / "o3"; sy.main([str(out3), "--collide"])
    rc = mb.open_toc((out3 / "tau-meters.bin").read_bytes())["rows"]
    check("--collide: rows 9 and 10 (TEST 09, TEST 10) share the low 16 bits of their id_hash and nothing else does", rc[8]["id"][4:8] == rc[9]["id"][4:8] and rc[8]["id"] != rc[9]["id"] and len({r["id"][4:8] for r in rc}) == 63)
    bad = 0
    for n in (7, 65):
        try: sy.build(n)
        except ValueError: bad += 1
    check("fewer than 8 or more than 64 rows are refused", bad == 2)
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
