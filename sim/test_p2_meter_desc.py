#!/usr/bin/env python3
"""The descriptor a pack carries (tools/player2/meter_desc.py builds it, fw2/cores/meter_desc_core.h reads it; B-812): for every meter manifest the C reader, the independent Python reader and the manifest agree on the name, the
Configure descriptors (label, type, range, step, choices, when), the presets, the preview palette and stream, the compose code and the limits; the descriptor is sound (md_check 0) and no read leaves it; every single-bit
flip of every descriptor is either refused or harmless, and NEVER reads outside; a wrong length, a preset table, a preview of the wrong size and a damaged trailer are refused.
Mutants: default ranges not checked; the preview size not checked."""
import json, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/player2"))
import meter_manifest as mm
import meter_desc as md
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(info)))
    fails += 0 if ok else 1
def build(td, bug):
    exe = Path(td) / ("md%d" % bug)
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-DBUG=%d" % bug, "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_meter_desc_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, blob, tmp, *args):
    f = tmp / "d.bin"; f.write_bytes(blob)
    return subprocess.run([str(exe), str(f)] + [str(a) for a in args], capture_output=True, text=True).stdout.strip().splitlines()
keys = sorted(p.parent.name for p in (ROOT / "meters").glob("*/meter.json") if "p2" in mm.load(p))
with tempfile.TemporaryDirectory() as td:
    tmp = Path(td); exe = build(tmp, 0)
    for key in keys:
        d = mm.load(ROOT / "meters" / key / "meter.json"); t = json.loads((ROOT / "meters" / key / "thumb.json").read_text())
        blob = md.build_desc(d, t); py = md.parse_desc(blob); out = run(exe, blob, tmp)
        rows = md.param_rows(d)
        ok = out[0] == "check 0 outside 0" and out[1] == "name %s cmds %d cmp %d maxc %d np %d npre %d mid %d slot %d" % (d["p2"].get("label", d["name"]), d["p2"].get("cmds", 0), md.cmp_code_int(d), d["p2"].get("max_cycles", 0), len(rows), 0, d["index"], d["p2"]["slot"])
        ps = [l for l in out if l.startswith("p ")]
        import mpreset as _mp
        pk = _mp.params_of(d)                                                                       # (key, hash, min, max, default): the descriptor carries the hash and the default (v2, B-967)
        want = ["p %s %d %d %d %d %d %d %d k%d d%d" % (r["label"], r["type"], r["hidden"], r["min"], r["max"], r["step"], r["when_idx"], r["when_val"], pk[i][1], pk[i][4]) + "".join(" [%s]" % v for v in r["vals"]) for i, r in enumerate(rows)]
        ok &= ps == want
        ok &= not [l for l in out if l.startswith("r ")]                                             # no preset table (v3, B-970)
        cs = [l for l in out if l.startswith("c ")]
        ok &= cs == ["c %d %d %d %d %d" % e for e in md.palette_entries(t)]
        ok &= out[-1] == "rle " + " ".join(str(b) for b in bytes.fromhex(t["rle"]))
        ok &= py["name"] == d["p2"].get("label", d["name"]) and len(py["params"]) == len(rows) and py["rle"] == bytes.fromhex(t["rle"]) and [(q["key"], q["def"]) for q in py["params"]] == [(x[1], x[4]) for x in pk]
        check("%s: the C reader, the Python reader and the manifest agree (%d B, %d parameters, no presets)" % (key, len(blob), len(rows)), ok, out[:2])
        fz = run(exe, blob, tmp, "fuzz")[0].split()
        check("%s: every single-bit flip is refused or harmless and none reads outside the descriptor (%s refused, %s accepted)" % (key, fz[4], fz[2]), fz[6] == "0", fz)
    blob = md.for_meter("waterfall_grid")
    check("a wrong length is refused", all(run(exe, blob, tmp, n)[0].split()[1] != "0" for n in (0, 7, len(blob) - 4, len(blob) + 4, len(blob) - 1)))
    po = int.from_bytes(blob[22:24], "little")                                                       # the first parameter record: key at +16, default at +18 (v2)
    b = bytearray(blob); b[po + 18:po + 20] = (65535).to_bytes(2, "little")
    check("a default outside its parameter's range is refused (code 4)", run(exe, bytes(b), tmp)[0].split()[1] == "4", run(exe, bytes(b), tmp)[0])
    b = bytearray(blob); b[po + 20 + 16:po + 20 + 18] = blob[po + 16:po + 18]
    check("two parameters with one key hash are refused (code 4)", run(exe, bytes(b), tmp)[0].split()[1] == "4", run(exe, bytes(b), tmp)[0])
    b = bytearray(blob); b[-1] ^= 1; check("a damaged trailer is refused", run(exe, bytes(b), tmp)[0].split()[1] != "0")
    b = bytearray(blob); b[9] = 1
    check("a descriptor that claims a preset table (v3 has none) is refused (code 1)", run(exe, bytes(b), tmp)[0].split()[1] == "1", run(exe, bytes(b), tmp)[0])
    rle_off = int.from_bytes(blob[28:30], "little"); b = bytearray(blob); b[rle_off] = (b[rle_off] & 0xE0) | (((b[rle_off] & 31) + 1) & 31)
    check("a preview of the wrong size is refused (code 7)", run(exe, bytes(b), tmp)[0].split()[1] == "7", run(exe, bytes(b), tmp)[0])
    for bug, name, bad in ((1, "default ranges are not checked", bytes(bytearray(blob[:po + 18]) + (65535).to_bytes(2, "little") + blob[po + 20:])), (2, "the preview size is not checked", None)):
        m = build(tmp, bug)
        if bad is None:
            b = bytearray(blob); b[rle_off] = (b[rle_off] & 0xE0) | (((b[rle_off] & 31) + 1) & 31); bad = bytes(b)
        check("mutant (%s) is caught" % name, run(m, bad, tmp)[0].split()[1] == "0")
# The host's Configure cache (fw2/ui/meters.c dyn_register): the numeric rows of a pack-only meter's descriptors are copied to hot RAM while it is edited, so a pack with more parameters than DYN_MAX_P is installed but REFUSED
# (status 42) and is not listed. The label and choice strings are not cached any more (read from the slot when a row is drawn, B-956): DEV 209 (B-954) lost Layered Wave from the list because 73 choice values did not fit a cache of 72.
import re
msrc = (ROOT / "fw2/ui/meters.c").read_text()
LIM = {n: int(re.search(r"#define %s (\d+)u" % n, msrc).group(1)) for n in ("DYN_MAX_P",)}
assert "DYN_POOL" not in msrc and "DYN_MAX_V" not in msrc, "the string caches are back: update this check"
for k in keys:
    pd = mm.load(ROOT / "meters" / k / "meter.json")
    if not pd["p2"].get("pack_only"): continue
    check("%s: its parameters fit the host's cache (%d of %d)" % (k, len(pd["params"]), LIM["DYN_MAX_P"]), len(pd["params"]) <= LIM["DYN_MAX_P"])
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
