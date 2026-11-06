#!/usr/bin/env python3
"""Meter presets from files inside the meter host (fw2/ui/meters.c with fw2/ui/mpreset_host.c, B-967): a pack-only meter's descriptor (v2: key hash and default per parameter) is registered, the slot driver is a directory
on the host holding <root>/tau-presets/<meter id>/ (the factory files of tools/player2/mpreset.py), and the scenarios of the firmware are walked through (Layered Wave, 38 parameters, 16 presets):
  1. with the files: the list is [DEFAULT] + the 16 presets in index order (17 places), position 0 runs on the parameters' defaults, position k on exactly the manifest's values of preset k-1; each file is read once
     (a preset read last is the tuning's base and is not read again);
  2. without an index file the meter keeps its descriptor's own presets (16) and nothing is read;
  3. a preset file that is missing or damaged leaves the meter as it was (values and selected position unchanged) and counts a failure; the others still work;
  4. a preset file written before the meter had three of its parameters still loads: those take their defaults;
  5. while the governor is not NORMAL nothing is read: the index is not loaded (the descriptor's presets stay) and a preset is not read (the meter keeps its values), and later, at NORMAL, both work (the busy answer is not remembered as a bad file);
  6. the remembered choice by id (B-968): the same preset after a reboot with the list reversed, the defaults when its id is gone, the tuning follows the id and goes with it, a pack's own list by position;
  7. mutants: a chosen preset applied without its file being read (the previous values stay) is caught by 1.
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
    # 6. the remembered choice (B-968): a preset file is remembered by its id, so a list that is reordered or loses it between two boots does the right thing
    def boot(args, tag, order=None, drop_ids=()):
        files = mp.factory_files(d); idx = [{"id": i, "name": p["name"].upper()[:15]} for i, p in enumerate(d["presets"]) if i not in drop_ids]
        if order == "reversed": idx = idx[::-1]
        files["%d/index.tpi" % mid] = mp.build_index(mid, idx)
        root = setup(td, tag, files=files)
        return subprocess.run([str(exe), str(desc), str(root), "0"] + [str(a) for a in args], capture_output=True, text=True, env=dict(os.environ, ROW=str(slot))).stdout.strip().splitlines()
    ch = boot(["choose", 4], "c1")                                                       # position 4 is the preset with id 3
    saved = ch[0].split(); tune0 = ch[1].split()[1:]
    check("choosing a preset file saves its id + 1 (4) and no position (0): %s" % ch[0], saved[2] == "4" and saved[4] == "0", ch[0])
    def now(o):
        a = o[0].split(" vals ")[0].split(); return {"pos": int(a[2]), "id1": int(a[4]), "custom": int(a[6]), "name": " ".join(a[8:a.index("npre")]), "npre": int(a[a.index("npre") + 1]), "vals": [int(x) for x in o[0].split(" vals ")[1].split()]}
    zero = ["0"] * 8
    r = now(boot(["restore", 4, 0] + zero, "r1", order="reversed"))
    check("the next boot, with the list REVERSED, selects the same preset (id 3: position 13 of 17) with its values", r["id1"] == 4 and r["pos"] == 13 and r["name"] == d["presets"][3]["name"].upper()[:15] and r["vals"] == [int(d["presets"][3]["values"][k]) for k in keys], (r["pos"], r["name"], r["id1"]))
    r = now(boot(["restore", 4, 0] + zero, "r2", drop_ids=(3,)))
    check("the next boot, with preset id 3 GONE from the list, goes to the defaults and forgets the choice (id1 %d, position %d)" % (r["id1"], r["pos"]), r["id1"] == 0 and r["pos"] == 0 and r["vals"] == [int(x[4]) for x in params], (r["id1"], r["pos"]))
    r = now(boot(["restore", 0, 0] + zero, "r3"))
    check("nothing remembered: the meter starts on its defaults (position 0)", r["pos"] == 0 and r["vals"] == [int(x[4]) for x in params])
    # the tuning follows the id
    ti = keys.index("layers"); ch = boot(["choose", 4, ti, 2], "c2"); tw = ch[1].split()[1:]
    check("a tuned preset file is saved with its id (custom %s) and a tuning" % ch[0].split()[6], ch[0].split()[6] == "1" and any(int(x) for x in tw), ch[:2])
    r = now(boot(["restore", 4, 0] + tw, "r4", order="reversed"))
    want = [int(d["presets"][3]["values"][k]) for k in keys]; want[ti] = 2
    check("the next boot, list reversed, applies the tuning to the same preset (layers 2, the rest as preset id 3, custom 1)", r["custom"] == 1 and r["vals"] == want, (r["custom"], r["vals"][:6], want[:6]))
    r = now(boot(["restore", 4, 0] + tw, "r5", drop_ids=(3,)))
    check("with preset id 3 gone the tuning is dropped with it (the defaults, custom 0)", r["custom"] == 0 and r["vals"] == [int(x[4]) for x in params], (r["custom"], r["vals"][:6]))
    # a pack's own list (no files): the position is remembered, as before
    nofile = setup(td, "own", files={})
    o = subprocess.run([str(exe), str(desc), str(nofile), "0", "choose", "5"], capture_output=True, text=True, env=dict(os.environ, ROW=str(slot))).stdout.strip().splitlines()
    check("without preset files the pack's own preset is remembered by its position (5) and no id: %s" % o[0], o[0].split()[2] == "0" and o[0].split()[4] == "5", o[0])
    # the built-in meter (Winamp Bars: no pack, its descriptors are in the firmware): the same files work through the generated key hashes and defaults
    wb = json.loads((ROOT / "meters/winamp_bars/meter.json").read_text()); wbp = mp.params_of(wb); wbk = [x[0] for x in wbp]
    rootb = setup(td, "bars", files=mp.factory_files(wb))
    o = subprocess.run([str(exe), str(desc), str(rootb), "0"], capture_output=True, text=True, env=dict(os.environ, ROW="0")).stdout.strip().splitlines()
    rb = parse(o); nb = len(wb["presets"])
    okb = rb["npre"] == nb + 1 and [rb["pos"][k]["name"] for k in range(nb + 1)] == ["DEFAULT"] + [x["name"].upper()[:15] for x in wb["presets"]] and rb["pos"][0]["vals"] == [x[4] for x in wbp]
    okb2 = all(rb["pos"][k + 1]["vals"] == [int(wb["presets"][k]["values"][key]) for key in wbk] for k in range(nb))
    check("the built-in Winamp Bars runs on preset files too: [DEFAULT] + %d presets, the defaults and every preset's values as the manifest says" % nb, okb and okb2 and rb["fails"] == 0, (rb["npre"], rb["fails"]))
    o = subprocess.run([str(exe), str(desc), str(setup(td, "bars_none", files={})), "0"], capture_output=True, text=True, env=dict(os.environ, ROW="0")).stdout.strip().splitlines()
    check("without files Winamp Bars keeps its own %d presets" % nb, parse(o)["npre"] == nb, parse(o)["npre"])
    # 7. mutants (the firmware code compiled with a fault): a preset file read but not applied, and the governor ignored
    m1 = parse(run(build(td, 11), desc, setup(td, "m1")))
    check("mutant (a preset file read and checked but not applied) is caught", m1["pos"][3]["vals"] != [int(d["presets"][2]["values"][key]) for key in keys])
    m3 = subprocess.run([str(build(td, 13)), str(desc), str(setup(td, "m3")), "0", "restore", "4", "0"] + ["0"] * 8, capture_output=True, text=True, env=dict(os.environ, ROW=str(slot))).stdout.strip().splitlines()
    check("mutant (the remembered id taken for a list position) is caught: the reordered list gives another preset", now(boot(["restore", 4, 0] + ["0"] * 8, "m3b", order="reversed"))["pos"] == 13 and subprocess.run([str(build(td, 13)), str(desc), str(setup(td, "m3c", files={**mp.factory_files(d), "%d/index.tpi" % mid: mp.build_index(mid, [{"id": i, "name": p["name"].upper()[:15]} for i, p in list(enumerate(d["presets"]))[::-1]])})), "0", "restore", "4", "0"] + ["0"] * 8, capture_output=True, text=True, env=dict(os.environ, ROW=str(slot))).stdout.split()[2] != "13")
    m2 = parse(run(build(td, 12), desc, root, 2))
    check("mutant (the governor ignored) is caught: it reads while the audio is under pressure", m2["npre"] == nd + 1)
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
