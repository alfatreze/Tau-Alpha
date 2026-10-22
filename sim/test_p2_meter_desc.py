#!/usr/bin/env python3
"""The descriptor a pack carries (tools/player2/meter_desc.py builds it, fw2/cores/meter_desc_core.h reads it; B-812): for every meter manifest the C reader, the independent Python reader and the manifest agree on the name, the
Configure descriptors (label, type, range, step, choices, when), the presets, the preview palette and stream, the compose code and the limits; the descriptor is sound (md_check 0) and no read leaves it; every single-bit
flip of every descriptor is either refused or harmless, and NEVER reads outside; a wrong length, a preset value outside its parameter's range, a preview of the wrong size and a damaged trailer are refused.
Mutants: preset ranges not checked; the preview size not checked."""
import json, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/player2"))
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
keys = sorted(p.parent.name for p in (ROOT / "meters").glob("*/meter.json") if "p2" in json.loads(p.read_text()))
with tempfile.TemporaryDirectory() as td:
    tmp = Path(td); exe = build(tmp, 0)
    for key in keys:
        d = json.loads((ROOT / "meters" / key / "meter.json").read_text()); t = json.loads((ROOT / "meters" / key / "thumb.json").read_text())
        blob = md.build_desc(d, t); py = md.parse_desc(blob); out = run(exe, blob, tmp)
        rows = md.param_rows(d)
        ok = out[0] == "check 0 outside 0" and out[1] == "name %s cmds %d cmp %d maxc %d np %d npre %d" % (d["p2"].get("label", d["name"]), d["p2"].get("cmds", 0), md.cmp_code_int(d), d["p2"].get("max_cycles", 0), len(rows), len(d["presets"]))
        ps = [l for l in out if l.startswith("p ")]
        want = ["p %s %d %d %d %d %d %d %d" % (r["label"], r["type"], r["hidden"], r["min"], r["max"], r["step"], r["when_idx"], r["when_val"]) + "".join(" [%s]" % v for v in r["vals"]) for r in rows]
        ok &= ps == want
        rs = [l for l in out if l.startswith("r ")]
        ok &= rs == ["r %s %s" % (p["name"], " ".join(str(p["values"][r["key"]]) for r in rows)) for p in d["presets"]]
        cs = [l for l in out if l.startswith("c ")]
        ok &= cs == ["c %d %d %d %d %d" % e for e in md.palette_entries(t)]
        ok &= out[-1] == "rle " + " ".join(str(b) for b in bytes.fromhex(t["rle"]))
        ok &= py["name"] == d["p2"].get("label", d["name"]) and len(py["params"]) == len(rows) and len(py["presets"]) == len(d["presets"]) and py["rle"] == bytes.fromhex(t["rle"])
        check("%s: the C reader, the Python reader and the manifest agree (%d B, %d parameters, %d presets)" % (key, len(blob), len(rows), len(d["presets"])), ok, out[:2])
        fz = run(exe, blob, tmp, "fuzz")[0].split()
        check("%s: every single-bit flip is refused or harmless and none reads outside the descriptor (%s refused, %s accepted)" % (key, fz[4], fz[2]), fz[6] == "0", fz)
    blob = md.for_meter("waterfall_grid")
    check("a wrong length is refused", all(run(exe, blob, tmp, n)[0].split()[1] != "0" for n in (0, 7, len(blob) - 4, len(blob) + 4, len(blob) - 1)))
    b = bytearray(blob); b[-1] ^= 1; check("a damaged trailer is refused", run(exe, bytes(b), tmp)[0].split()[1] != "0")
    py = md.parse_desc(blob); presets_off = int.from_bytes(blob[24:26], "little")
    b = bytearray(blob); b[presets_off + 2 + 2 * 3: presets_off + 2 + 2 * 3 + 2] = (65535).to_bytes(2, "little")      # preset 0, the cell size, far above its maximum
    check("a preset value outside its parameter's range is refused (code 5)", run(exe, bytes(b), tmp)[0].split()[1] == "5", run(exe, bytes(b), tmp)[0])
    rle_off = int.from_bytes(blob[28:30], "little"); b = bytearray(blob); b[rle_off] = (b[rle_off] & 0xE0) | (((b[rle_off] & 31) + 1) & 31)
    check("a preview of the wrong size is refused (code 7)", run(exe, bytes(b), tmp)[0].split()[1] == "7", run(exe, bytes(b), tmp)[0])
    for bug, name, bad in ((1, "preset ranges are not checked", bytes(bytearray(blob[:presets_off + 8]) + (65535).to_bytes(2, "little") + blob[presets_off + 10:])), (2, "the preview size is not checked", None)):
        m = build(tmp, bug)
        if bad is None:
            b = bytearray(blob); b[rle_off] = (b[rle_off] & 0xE0) | (((b[rle_off] & 31) + 1) & 31); bad = bytes(b)
        check("mutant (%s) is caught" % name, run(m, bad, tmp)[0].split()[1] == "0")
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
