#!/usr/bin/env python3
"""Meter presets as separate files (fw2/cores/mpreset_core.h, tools/player2/mpreset.py; docs/features/METER_PRESETS_EXTERNAL.md, B-966). The real firmware reader, compiled on the host, reads files the Python writer made:
  1. the factory set of every Player 2 meter (from its manifest): the index and every preset check out, and the values the firmware applies are exactly the manifest preset's values;
  2. the property the old METR section lacked: a preset written for a meter with FEWER parameters (a key missing) keeps working, the new parameter takes its default; a key the meter does not have is ignored;
  3. a value outside a parameter's range is clamped (the writer refuses to write one, so the file is made by hand);
  4. refusals: a wrong meter id, a wrong preset id, a truncated file, an oversize file, and EVERY single-byte change of every factory file of a meter (the CRC covers it);
  5. the index: 200 presets (the most one 4 KB read holds), names, hidden flag, the position of an id after a reorder;
  6. the writer refuses: an unknown key, an out-of-range value, a 16-character name, two parameter keys with one hash (checked over every manifest's keys);
  7. mutants: the clamp left out is caught by 3.
Host-simulated only."""
import json, struct, subprocess, sys, tempfile, zlib
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/player2"))
import mpreset as mp
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(info)))
    fails += 0 if ok else 1
def build(td, bug=0):
    exe = Path(td) / ("h%d" % bug)
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-DBUG=%d" % bug, "-o", str(exe), str(ROOT / "sim/p2_mpreset_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, *args): return subprocess.run([str(exe)] + [str(a) for a in args], capture_output=True, text=True).stdout.strip().splitlines()
def params_file(td, params):
    f = Path(td) / "params.txt"; f.write_text("\n".join("%d %d %d %d" % (h, lo, hi, d) for _, h, lo, hi, d in params) + "\n"); return f
def pre(exe, td, data, meter_id, ident, params):
    f = Path(td) / "x.bin"; f.write_bytes(data)
    o = run(exe, "pre", f, meter_id, ident, params_file(td, params))
    head = o[0].split(" ", 3)
    return int(head[1]), int(head[2]), (o[1].split() if len(o) > 1 else [])
mans = {}
for p in sorted((ROOT / "meters").glob("*/meter.json")):
    d = json.loads(p.read_text())
    if "p2" in d: mans[d["key"]] = d
with tempfile.TemporaryDirectory() as td:
    exe = build(td); tmp = Path(td)
    # 1. the factory set of every meter
    for key, d in mans.items():
        params = mp.params_of(d); files = mp.factory_files(d); mid = d["index"]; keys = [p[0] for p in params]
        f = tmp / "i.bin"; f.write_bytes(files[key + "/index.tpi"]); o = run(exe, "idx", f, mid)
        ok = o[0] == "idx 0 %d" % len(d["presets"]) and [l.split(" ", 2)[2] for l in o[1:]] == [p["name"].upper()[:15] for p in d["presets"]]
        good = True
        for i, pr in enumerate(d["presets"]):
            code, hit, vals = pre(exe, td, files["%s/p%d.tpr" % (key, i)], mid, i, params)
            want = [str(pr["values"][k]) if k in pr["values"] else str(dflt) for k, _, _, _, dflt in params]
            good = good and code == 0 and vals == want and hit == len([k for k in keys if k in pr["values"]])
        check("%s: the index (%d presets) and every preset check out and apply exactly the manifest's values" % (key, len(d["presets"])), ok and good, (o[:2], key))
    # 2. a meter that gained a parameter keeps its preset; a key the meter does not have is ignored
    d = mans["layered_wave"]; params = mp.params_of(d); mid = d["index"]
    older = [p for p in params if p[0] not in ("taper_head", "taper_tail", "direction")]                       # the meter as it was before B-958/B-961
    data = mp.build_preset(mid, 3, "OLD SET", {k: v for k, v in d["presets"][3]["values"].items() if k in [p[0] for p in older]}, params)
    code, hit, vals = pre(exe, td, data, mid, 3, params)
    idx = {p[0]: i for i, p in enumerate(params)}
    check("a preset written before three parameters existed still loads: those take their defaults (%s)" % [vals[idx[k]] for k in ("taper_head", "taper_tail", "direction")], code == 0 and [vals[idx[k]] for k in ("taper_head", "taper_tail", "direction")] == [str(params[idx[k]][4]) for k in ("taper_head", "taper_tail", "direction")] and vals[idx["layers"]] == str(d["presets"][3]["values"]["layers"]))
    code, hit, vals = pre(exe, td, data, mid, 3, older)
    check("and a meter that LOST parameters ignores the keys it no longer has (%d keys matched of %d)" % (hit, len(params)), code == 0 and hit == len(older) - len([p for p in older if p[0] not in d["presets"][3]["values"]]))
    # 3. clamping (a file made by hand: the writer would refuse it)
    h = params[idx["layers"]][1]; lo, hi = params[idx["layers"]][2:4]
    body = struct.pack("<BB", 1, 2) + b"HI" + struct.pack("<HH", h, 60000)
    hand = b"TPRS" + struct.pack("<BBH", 1, 9, mid) + struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF) + body
    code, hit, vals = pre(exe, td, hand, mid, 9, params)
    check("a value outside the range is clamped to its maximum (%d -> %s)" % (60000, vals[idx["layers"]] if vals else ""), code == 0 and vals and vals[idx["layers"]] == str(hi))
    # 4. refusals
    good = mp.factory_files(d)["layered_wave/p0.tpr"]
    check("a wrong meter id is refused (5)", pre(exe, td, good, mid + 1, 0, params)[0] == 5)
    check("a wrong preset id is refused (6)", pre(exe, td, good, mid, 1, params)[0] == 6)
    check("a truncated file is refused", all(pre(exe, td, good[:n], mid, 0, params)[0] != 0 for n in (0, 5, 13, len(good) - 1)))
    check("an oversize file is refused (1)", pre(exe, td, good + b"\0" * 400, mid, 0, params)[0] == 1)
    bad = 0
    for key in ("layered_wave", "chladni"):
        dd = mans[key]; pp = mp.params_of(dd)
        for rel, data in mp.factory_files(dd).items():
            for i in range(len(data)):
                b = bytearray(data); b[i] ^= 0x5A
                if rel.endswith(".tpi"):
                    f = tmp / "i.bin"; f.write_bytes(bytes(b)); r = run(exe, "idx", f, dd["index"])[0].split()[1] == "0"
                else:
                    r = pre(exe, td, bytes(b), dd["index"], int(rel.split("/p")[1].split(".")[0]), pp)[0] == 0
                bad += r
    check("every single-byte change of every factory file of Layered Wave and Chladni is refused (%d accepted)" % bad, bad == 0)
    def refuses(fn):
        try: fn(); return False
        except ValueError: return True
    # 5. the index
    many = [{"id": i, "name": "PRESET %d" % i, "hidden": i % 7 == 0} for i in range(200)]
    f = tmp / "i.bin"; f.write_bytes(mp.build_index(mid, many)); o = run(exe, "idx", f, mid)
    check("an index of 200 presets (the most one 4 KB read holds) reads back (names, ids, the hidden flag)", o[0] == "idx 0 200" and o[1] == "0 1 PRESET 0" and o[-1] == "199 0 PRESET 199", (o[0], o[1] if len(o) > 1 else "", o[-1]))
    check("an index of 201 presets is refused by the writer, and by the reader when one is made by hand", refuses(lambda: mp.build_index(mid, [{"id": i, "name": "P"} for i in range(201)])))
    reordered = mp.build_index(mid, [{"id": 7, "name": "SEVEN"}, {"id": 2, "name": "TWO"}, {"id": 40, "name": "FORTY"}]); f.write_bytes(reordered)
    check("the position of an id follows the list: ids 7, 2, 40 are at 0, 1, 2 and id 5 is not there", [run(exe, "find", f, i)[0] for i in (7, 2, 40, 5)] == ["find 0", "find 1", "find 2", "find -1"])
    # 6. the writer refuses
    check("the writer refuses an unknown key, an out-of-range value and a 16-character name", refuses(lambda: mp.build_preset(mid, 0, "X", {"nope": 1}, params)) and refuses(lambda: mp.build_preset(mid, 0, "X", {"layers": 99}, params)) and refuses(lambda: mp.build_preset(mid, 0, "X" * 16, {}, params)))
    check("no two parameter keys of any meter share a hash", all(mp.params_of(dd) for dd in mans.values()))
    # 7. the mutant
    m = build(td, 1)
    code, hit, vals = pre(m, td, hand, mid, 9, params)
    check("mutant (no clamp) is caught", vals[idx["layers"]] != str(hi))
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
