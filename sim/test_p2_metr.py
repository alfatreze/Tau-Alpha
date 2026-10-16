#!/usr/bin/env python3
"""METR reader (fw2/cores/metr_core.h) against METR sections the Python writer (tools/tau_assets.py) produced: for five meters the C reader's names, values and boot preset equal the Python reader's; a flipped byte anywhere is
refused (CRC); an entry for a meter with a different parameter count is skipped alone; an out-of-range value is clamped. Mutant: no clamp."""
import json, struct, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import tau_assets as ta
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    if not ok: fails += 1
def build(td, bug):
    exe = Path(td) / f"m{bug}"
    r = subprocess.run(["cc", "-O1", "-w", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_metr_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def spec():
    reg = ta.registry(); ms = []
    for key, n in (("winamp_bars", 3), ("winamp_scope", 2), ("chladni", 4), ("vu_master", 2), ("layered_wave", 5)):
        m = reg[key]; pres = []
        for i, p in enumerate(m["presets"][:n]):
            pres.append({"name": ("USER %d" % i), "values": dict(p["values"])})
        ms.append({"key": key, "default_preset": n - 1, "presets": pres})
    return {"meters": ms}
def run(exe, blob, td):
    f = Path(td) / "metr.bin"; f.write_bytes(blob)
    return subprocess.run([str(exe), str(f)], capture_output=True, text=True).stdout.strip().split("\n")
def expected(blob):
    out = ["check 0"]
    reg = {m["id"]: m for m in ta.registry().values()}
    parsed = {e["id"]: e for e in ta.parse_meters(blob)}
    for mid in (12, 13, 14, 15, 16):
        e = parsed[mid]; out.append(f"{mid} {len(e['presets'])} {e['default_preset']}")
        for p in e["presets"]: out.append(p["name"] + "".join(" %d" % p["values"][k["key"]] for k in reg[mid]["params"]))
    return out
with tempfile.TemporaryDirectory() as td:
    g = build(td, 0); blob = ta.pack_meters(spec())
    check("names, values and boot preset equal the Python reader's (five meters)", run(g, blob, td) == expected(blob))
    bad = 0
    for i in range(12, len(blob), 7):
        b = bytearray(blob); b[i] ^= 0x40
        if run(g, bytes(b), td)[0] == "check 0": bad += 1
    check("a flipped byte anywhere is refused", bad == 0)
    # an entry whose parameter count is wrong is skipped alone
    reg = ta.registry(); s = spec(); blob2 = bytearray(ta.pack_meters({"meters": [s["meters"][0]]}))
    pos = 14 + 2; blob2[pos + 5] = 7                                                     # nparams of the first (only) entry: 7 instead of 8
    blob2[8:12] = struct.pack("<I", ta.crc(bytes(blob2[12:])))
    r = run(g, bytes(blob2), td); check("a parameter-count mismatch skips that meter only", r[0] == "check 0" and r[1] == "12 none")
    # clamping: write a value above its max (bypassing the writer's check)
    blob3 = bytearray(ta.pack_meters({"meters": [{"key": "winamp_scope", "default_preset": 0, "presets": [{"name": "X", "values": {"scope_smooth": 10, "scope_trail": 10}}]}]}))
    first_val = 14 + 2 + 6 + 16; blob3[first_val] = 250                                  # scope_smooth max is 90
    blob3[8:12] = struct.pack("<I", ta.crc(bytes(blob3[12:])))
    r = run(g, bytes(blob3), td); line = next(l for l in r if l.startswith("X "))
    check("an out-of-range value is clamped to the compiled range", line.split()[1] == str(ta.registry()["winamp_scope"]["params"][0]["max"]))
    b = build(td, 1); r = run(b, bytes(blob3), td); line = next(l for l in r if l.startswith("X "))
    check("mutant (no clamp) is caught", line.split()[1] == "250")
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
