#!/usr/bin/env python3
"""Player 2 aout + tone transport (fw2/aout/aout.c, fw2/transport/tone.c): the REAL module code against a model of the ring reader, FIFO, hold bit and flush (sim/p2_aout_host.c).
A ground-truth ledger checks, at every loop, the heard segment and position, the window contents the reader sees, the accounting, a double-precision sine reference, and
that no pair vanishes (put = consumed + dropped by flushes + still queued). Stalls up to 500 ms are injected; the ring must absorb every one. Mutants must each fail."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    if not ok: fails += 1
def build(d, defs):
    exe = d / ("h" + "_".join(defs).replace("=", "") if defs else "h0")
    r = subprocess.run(["cc", "-O1", "-Wall", "-Werror", "-Wno-unused-variable", "-I", str(ROOT / "fw2/cores"), "-I", str(ROOT / "fw2"), *[f"-D{x}" for x in defs], "-o", str(exe), str(ROOT / "sim/p2_aout_host.c"), "-lm"], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, sc):
    out = subprocess.run([str(exe), str(sc)], capture_output=True, text=True, timeout=120).stdout.split()
    return {out[i]: int(out[i + 1]) for i in range(0, len(out) - 1, 2)}
def base(r):
    return (r["DATA_MISMATCH"] == 0 and r["HEARD_MISMATCH"] == 0 and r["HEARD_CHECKED"] > 20000 and r["REF_BAD"] == 0 and r["ACCT_ERR"] == 0 and r["ACCT_BAD"] == 0 and r["OVERFLOW"] == 0
            and r["UNDER_RING"] == 0 and r["DRY"] == 0 and r["PUT_PAIRS"] == r["CONSUMED"] + r["DROPPED_UNREAD"] + r["DROPPED_FIFO"] + r["ENDFILL"])
def good0(r): return base(r) and r["FLUSHES"] >= 5
def good2(r):
    return (base(r) and r["SWAPS"] == 6 and 3527990 <= r["CONSUMED"] <= 3528010 and r["STALLS_INJECTED"] > 50)
def good1(r): return r["MARGIN_MISMATCH"] == 0 and r["STALLS"] > 0 and r["DATA_MISMATCH"] == 0
with tempfile.TemporaryDirectory() as td:
    d = Path(td)
    g = build(d, [])
    r0, r1, r2 = run(g, 0), run(g, 1), run(g, 2)
    check(f"keys, flushes, a hold, a rate change and {r0['STALLS_INJECTED']} stalls of up to 500 ms: data, heard segment and position, sine, accounting and conservation all exact ({r0['HEARD_CHECKED']} heard checks)", good0(r0))
    check(f"80 s of plain play: {r2['SWAPS']} gapless swaps (one per 12 s), {r2['CONSUMED']} pairs heard (80 s x 44,100), no underrun, no dry spell, {r2['STALLS_INJECTED']} stalls absorbed", good2(r2))
    check(f"filling the ring to its limit never overwrites the margin behind the read index ({r1['STALLS']} refused puts)", good1(r1))
    muts = [(["AOUT_BUG=2"], "the write index wraps one slot late", 0, good0), (["AOUT_BUG=3"], "no margin behind the read index", 1, good1), (["AOUT_BUG=4"], "the write index is never published", 0, good0),
            (["TONE_BUG=5"], "a restart does not restart the timeline", 0, good0), (["TONE_BUG=6"], "the sine's phase restarts at every segment", 0, good0),
            (["TONE_BUG=7"], "the heard side never pops finished segments", 2, good2), (["TONE_BUG=8"], "every segment is one pair short (the accounting must say so)", 2, good2)]
    for defs, nm, sc, ok in muts:
        r = run(build(d, defs), sc)
        check(f"mutant caught: {nm}", not ok(r))
print("PASSED" if not fails else f"{fails} FAILED"); sys.exit(1 if fails else 0)
