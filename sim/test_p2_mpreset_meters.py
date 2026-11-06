#!/usr/bin/env python3
"""Meter presets from files inside the meter host (fw2/ui/meters.c with fw2/ui/mpreset_host.c, B-967): a pack-only meter's descriptor (v2: key hash and default per parameter) is registered, the slot driver is a directory
on the host holding <root>/tau-presets/<meter id>/ (the factory files of tools/player2/mpreset.py), and the scenarios of the firmware are walked through (Layered Wave, 38 parameters, 16 presets):
  1. with the files: the list is [DEFAULT] + the 16 presets in index order (17 places), position 0 runs on the parameters' defaults, position k on exactly the manifest's values of preset k-1; each file is read once
     (a preset read last is the tuning's base and is not read again);
  2. without an index file the meter keeps its descriptor's own presets (16) and nothing is read;
  3. a preset file that is missing or damaged leaves the meter as it was (values and selected position unchanged) and counts a failure; the others still work;
  4. a preset file written before the meter had three of its parameters still loads: those take their defaults;
  5. while the governor is not NORMAL nothing is read: the index is not loaded (the descriptor's presets stay) and a preset is not read (the meter keeps its values), and later, at NORMAL, both work (the busy answer is not remembered as a bad file);
  6. mutants: a chosen preset applied without its file being read (the previous values stay) is caught by 1.
Host-simulated only (the card is a directory; the real read time is B-964's measurement: about 33 ms)."""
import json, os, shutil, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/player2"))
import meter_desc as md, mpreset as mp
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(info)))
    fails += 0 if ok else 1
d = json.loads((ROOT / "meters/layered_wave/meter.json").read_text()); t = json.loads((ROOT / "meters/layered_wave/thumb.json").read_text())
params = mp.params_of(d); keys = [p[0] for p in params]; mid = d["index"]; slot = d["p2"]["slot"]
def build(td, mut=None):
    exe = Path(td) / ("h%s" % (mut or ""))
    cmd = ["cc", "-std=gnu11", "-O1", "-w", "-DP2_HOST"] + (["-DBUG=%d" % mut] if mut else []) + ["-include", str(ROOT / "sim/p2_host_regs.h"), "-I", str(ROOT / "sim"), "-o", str(exe), str(ROOT / "sim/p2_mpreset_meters_harness.c")]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode: print(r.stderr[:1500]); sys.exit(1)
    return exe
def run(exe, desc, root, degrade=0):
    env = dict(os.environ)
    o = subprocess.run([str(exe), str(desc), str(root), str(degrade)], capture_output=True, text=True, env=dict(env, ROW=str(slot)))
    return o.stdout.strip().splitlines()
def parse(out):
    res = {"npre": None, "pos": {}, "fails": None, "reads": None, "head": out[0] if out else ""}
    for l in out:
        if l.startswith("npre"): res["npre"] = int(l.split()[1]); res["idx_ok"] = int(l.split()[3]); res["err"] = int(l.split()[5])
        elif l.startswith("pos"):
            a = l.split(" vals ")[0].split(); res["pos"][int(a[1])] = {"sel": int(a[3]), "name": " ".join(a[5:a.index("reads")]), "reads": int(a[a.index("reads") + 1][1:]), "vals": [int(x) for x in l.split(" vals ")[1].split()]}
        elif l.startswith("fails"): res["fails"] = int(l.split()[1]); res["reads"] = int(l.split()[3])
    return res
def setup(td, name, files=None, drop=(), flip=()):
    root = Path(td) / name
    base = mp.factory_files(d) if files is None else files
    for rel, data in base.items():
        if rel in drop: continue
        if rel in flip: data = bytes(data[:20]) + bytes([data[20] ^ 1]) + bytes(data[21:])
        f = root / "tau-presets" / rel; f.parent.mkdir(parents=True, exist_ok=True); f.write_bytes(data)
    root.mkdir(parents=True, exist_ok=True)
    return root
with tempfile.TemporaryDirectory() as td:
    tmp = Path(td); desc = tmp / "lw.desc"; desc.write_bytes(md.build_desc(d, t))
    exe = build(td)
    nd = len(d["presets"])
    # 1. the files
    r = parse(run(exe, desc, setup(td, "all")))
    names = ["DEFAULT"] + [p["name"].upper()[:15] for p in d["presets"]]
    want0 = [str(x[4]) for x in params]
    ok = r["npre"] == nd + 1 and r["idx_ok"] == 1 and [r["pos"][k]["name"] for k in range(nd + 1)] == names and r["pos"][0]["vals"] == [int(x) for x in want0]
    ok2 = all(r["pos"][k + 1]["vals"] == [int(d["presets"][k]["values"][key]) for key in keys] and r["pos"][k + 1]["sel"] == k + 1 and r["pos"][k + 1]["reads"] == 1 for k in range(nd))
    check("with the files the list is [DEFAULT] + %d presets in index order, position 0 on the defaults, position k on the manifest's preset k-1, each file read once" % nd, ok and ok2 and r["fails"] == 0 and r["reads"] == nd, (r["head"], r["npre"], r["fails"], r["reads"]))
    # 2. no index file: the descriptor's presets
    r = parse(run(exe, desc, setup(td, "noindex", drop=("%d/index.tpi" % mid,))))
    check("without an index file the meter keeps its descriptor's %d presets and reads nothing (npre %s, reads %s)" % (nd, r["npre"], r["reads"]), r["npre"] == nd and r["reads"] == 0 and r["fails"] == 0)
    # 3. a missing and a damaged preset file
    r = parse(run(exe, desc, setup(td, "bad", drop=("%d/p2.tpr" % mid,), flip=("%d/p4.tpr" % mid,))))
    check("a missing preset file (position 3) and a damaged one (position 5) leave the meter as it was: the selected position and the values do not change", r["pos"][3]["sel"] == 2 and r["pos"][3]["vals"] == r["pos"][2]["vals"] and r["pos"][5]["sel"] == 4 and r["pos"][5]["vals"] == r["pos"][4]["vals"], (r["pos"][3]["sel"], r["pos"][5]["sel"]))
    check("they count two failures and every other preset still works (%d reads of %d)" % (r["reads"], nd - 2), r["fails"] == 2 and r["reads"] == nd - 2 and r["pos"][6]["vals"] == [int(d["presets"][5]["values"][key]) for key in keys])
    # 4. a preset written before three parameters existed
    older = {k: v for k, v in d["presets"][3]["values"].items() if k not in ("taper_head", "taper_tail", "direction")}
    files = mp.factory_files(d); files["%d/p3.tpr" % mid] = mp.build_preset(mid, 3, d["presets"][3]["name"].upper()[:15], older, params)
    r = parse(run(exe, desc, setup(td, "older", files=files)))
    want = [int(d["presets"][3]["values"][key]) if key in older else params[i][4] for i, key in enumerate(keys)]
    check("a preset written before three parameters existed still loads, those three take their defaults", r["pos"][4]["vals"] == want, (r["pos"][4]["vals"], want))
    # 5. the governor busy
    root = setup(td, "busy")
    r = parse(run(exe, desc, root, 2))
    check("while the governor is not NORMAL the index is not read (the descriptor's %d presets stay, nothing read)" % nd, r["npre"] == nd and r["reads"] == 0 and r["err"] == 10, (r["npre"], r["reads"], r["err"]))
    # 6. mutants (the firmware code compiled with a fault): a preset file read but not applied, and the governor ignored
    m1 = parse(run(build(td, 11), desc, setup(td, "m1")))
    check("mutant (a preset file read and checked but not applied) is caught", m1["pos"][3]["vals"] != [int(d["presets"][2]["values"][key]) for key in keys])
    m2 = parse(run(build(td, 12), desc, root, 2))
    check("mutant (the governor ignored) is caught: it reads while the audio is under pressure", m2["npre"] == nd + 1)
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
