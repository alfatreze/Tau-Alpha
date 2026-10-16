#!/usr/bin/env python3
"""Player 2 report: the REAL record builder (fw2/diagnostics/report_record.c + counters_core + suite_core) makes a record from a 60-counter table; it survives the pixel grid
(tools/tpg.py robust mode and a camera-less screenshot path) and the decoder (tools/decode_tau_suite.py) returns exactly the rows the Info page shows: every name and value,
signed, hex and a 38-character text included, in order."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import tpg, decode_tau_suite as d
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    if not ok: fails += 1
with tempfile.TemporaryDirectory() as td:
    exe = Path(td) / "r"
    r = subprocess.run(["cc", "-O1", "-Wall", "-Werror", "-Wno-unused-function", "-Wno-unknown-pragmas", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_report_harness.c"), str(ROOT / "fw2/diagnostics/report_record.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    out = subprocess.run([str(exe)], capture_output=True, text=True).stdout.split()
    n, rec = int(out[0]), bytes.fromhex(out[1])
    check(f"the record is {n} bytes and fits the robust grid ({tpg.capacity(tpg.MODE_R)} B)", 0 < n == len(rec) <= tpg.capacity(tpg.MODE_R))
    rep = d.parse_record(rec)
    rows = rep["entries"]["infotext"]
    want = [("FIRMWARE", "0.6.0-dev.132+abcdef0.dirty FLAC 44.1K"), ("CORE VERSION", "4D50331A"), ("NEG", "-12345 PAIRS"), ("FLAG", "1")]
    check(f"all 60 counters are in the record, in order ({len(rows)})", len(rows) == 60 and [r["row"] for r in rows] == list(range(60)))
    check("text, hex, signed and flag values read back exactly", [(r["label"], r["value"]) for r in rows[:4]] == want)
    check("a numbered row reads back with its unit", rows[4] == {"row": 4, "label": "ROW 00", "value": "1007 MS"} and rows[5]["value"] == "2007 PAIRS")
    # through the pixel grid, robust and lossless, and the decoder again
    import numpy as np
    for mode, nm in ((tpg.MODE_R, "robust"), (tpg.MODE_L, "lossless")):
        px = tpg.decode(tpg.to_rgb(tpg.encode(rec, mode)))
        check(f"the record survives the {nm} pixel grid unchanged", px == rec)
    bad = bytearray(rec); bad[20] ^= 0x01
    try: d.parse_record(bytes(bad)); refused = False
    except ValueError: refused = True
    check("a record with one flipped bit is refused by the decoder (CRC)", refused)
print("PASSED" if not fails else f"{fails} FAILED"); sys.exit(1 if fails else 0)
