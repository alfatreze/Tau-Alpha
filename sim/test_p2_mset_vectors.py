#!/usr/bin/env python3
"""The golden vectors of the meter-settings save file (tools/meter_settings_vectors.py -> docs/research/meter_settings/, for Tau Omega, O026/3): the filed folder regenerates byte for byte; expected.json is what the Python
reference answers; the firmware's C code (fw2/cores/mset_file_core.h, sim/p2_mset_file_harness.c) answers every 8,192-byte vector the same way (active bank, seq, validity, record lookups, locked); an edit over the seq-wrap
files gives the same bytes from C and Python (including seq 0xFFFFFFFF + 1 = 0); and a C core that compares seq as a plain number (mutant 5) FAILS on the wrap files."""
import hashlib, json, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import meter_settings as ms
import meter_settings_vectors as mv
import toolchain
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(extra)))
    if not ok: fails += 1
VD = ROOT / "docs/research/meter_settings"
exp = json.loads((VD / "expected.json").read_text())
def build(td, bug=0):
    exe = Path(td) / f"mset{bug}"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", f"-DBUG={bug}", "-o", str(exe), str(ROOT / "sim/p2_mset_file_harness.c")], capture_output=True, text=True, env=toolchain.host_env())
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, td, data, *args, tear=-1):
    i, o = Path(td) / "in.bin", Path(td) / "out.bin"; i.write_bytes(data)
    r = subprocess.run([str(exe), str(i), str(o), str(tear), *args], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    return o.read_bytes(), r.stdout.strip()
def c_scan(exe, td, data):
    return dict(kv.split("=") for kv in run(exe, td, data, "scan")[1].split())
def c_find(exe, td, data, idh, rev):
    out = run(exe, td, data, "find", str(idh), str(rev))[1]
    d = dict(kv.split("=") for kv in out.split(" ")[:3] if "=" in kv)
    n = int(out.split("n=")[1].split()[0]) if "n=" in out else 0
    pairs = [list(map(int, p.split(":"))) for p in out.split("n=")[1].split(" ", 1)[1].split(",")] if n else []
    return int(d["found"]), int(d["rev_ok"]), int(d["preset"]), pairs
with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    # 1. the folder regenerates, and the filed answers are the reference's
    out = td / "regen"; sys.argv = ["x", str(out)]; mv.main()
    same = sorted(p.name for p in out.iterdir()) == sorted(p.name for p in VD.iterdir()) and all((out / p.name).read_bytes() == p.read_bytes() for p in VD.iterdir())
    check("the filed folder (%d vectors, expected.json, README) regenerates byte for byte" % len(exp["files"]), same)
    bad = [f["file"] for f in exp["files"] if hashlib.sha256((VD / f["file"]).read_bytes()).hexdigest() != f["sha256"] or len((VD / f["file"]).read_bytes()) != f["bytes"]]
    check("every file has the size and SHA-256 expected.json says", not bad, bad)
    exe = build(td)
    # 2. C against expected.json, for every 8,192-byte file
    bad = []
    for f in exp["files"]:
        data = (VD / f["file"]).read_bytes()
        if len(data) != 8192: continue
        sc = c_scan(exe, td, data)
        locked = sc["locked"] == "1"
        if locked != (f["status"] == "locked"): bad.append((f["file"], "locked"))
        if f["status"] in ("ok",):
            if int(sc["active"]) != f["active_bank"] or int(sc["seq0"] if f["active_bank"] == 0 else sc["seq1"]) != f["seq"]: bad.append((f["file"], "bank/seq", sc))
            if [sc["valid0"] == "1", sc["valid1"] == "1"] != [g["valid"] for g in f["generations"]]: bad.append((f["file"], "validity"))
            if int(sc["count0"] if f["active_bank"] == 0 else sc["count1"]) != f["count"]: bad.append((f["file"], "count"))
            for l in f["lookups"]:
                fo, rok, pr, pairs = c_find(exe, td, data, int(l["id_hash"], 16), l["params_rev"])
                if (fo and rok) != l["found"] or (l["found"] and (pr != l["preset"] or pairs != l["entries"])): bad.append((f["file"], "lookup", l))
        elif f["status"] == "none":
            if sc["active"] != "-1": bad.append((f["file"], "none"))
    check("the firmware's C code answers every 8,192-byte vector as expected.json says (locked, active bank, seq, validity, count, every lookup incl. the wrong-params_rev ones)", not bad, bad[:3])
    # 3. an edit over the wrap files and over a file whose seq is 0xFFFFFFFF
    wrap_only = ms._build(0xFFFFFFFF, [{"id_hash": mv.A, "params_rev": 1, "preset": 0, "entries": [(0, 1)]}])
    wrap_file = mv.set_bank(mv.ZERO, 0, wrap_only)
    ok = True
    for name, data in [(f["file"], (VD / f["file"]).read_bytes()) for f in exp["files"] if f["file"].startswith("seq_")] + [("only seq 0xFFFFFFFF", wrap_file)]:
        c_out, _ = run(exe, td, data, "put", str(mv.B), "1", "0", "1:7")
        p_out = ms.apply_change(data, mv.B, 1, 0, [(1, 7)])
        if c_out != p_out: ok = False; print("   differs:", name)
    check("an edit over each seq file (and over a file whose only seq is 0xFFFFFFFF, so the next seq is 0) gives the same 8,192 bytes from C and Python", ok)
    ms2 = ms.parse(ms.apply_change(wrap_file, mv.B, 1, 0, [(1, 7)]))
    check("the generation after seq 0xFFFFFFFF has seq 0 and wins (the file does not get stuck on the old generation)", ms2["seq"] == 0 and ms2["active"] == 1 and len(ms2["records"]) == 2, ms2["seq"])
    # 4. the plain-number mutant fails on the wrap files
    mexe = build(td, 5); caught = []
    for f in exp["files"]:
        if not f["file"].startswith("seq_"): continue
        sc = c_scan(mexe, td, (VD / f["file"]).read_bytes())
        if int(sc["active"]) != f["active_bank"]: caught.append(f["file"])
    check("mutant must FAIL: seq compared as a plain unsigned number picks the wrong bank on %s" % ", ".join(caught), set(caught) == {"seq_wrap_b_wins.bin", "seq_wrap_a_wins.bin"}, caught)
print("FAILED" if fails else "all ok")
sys.exit(1 if fails else 0)
