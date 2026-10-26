#!/usr/bin/env python3
"""Player 2 stream reader (fw2/cores/stream_core.h, spec section 9.3): a time-stepped card model (one command at a time, latency + jitter, a long outlier, reads past the
end failing, transient failures right after an open) feeds the real C core while a consumer checks every byte against the file. Properties: bytes are exact; never two
commands in flight; no read wraps the ring; read-ahead keeps a nominal stream from starving; a seek leaves no stale byte; end of file is found exactly (known size) or by
three failures at the minimum read (unknown size); a long stall is counted and survived. Five mutants must each be caught."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + (("  " + extra) if (extra and not ok) else ""))
    if not ok: fails += 1
def build(d, bug):
    exe = d / f"s{bug}"
    r = subprocess.run(["cc", "-O1", "-Wall", "-Werror", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_stream_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, known=1, size=200000, rate=40000, frame=1152, lat=6000, jit=2000, outl=-1, outl_ms=0, settle=0, seek_ms=-1, seek_to=0, maxms=20000, cap=24576, seed=1):
    out = subprocess.run([str(exe), *map(str, (known, size, rate, frame, lat, jit, outl, outl_ms, settle, seek_ms, seek_to, maxms, cap, seed))], capture_output=True, text=True).stdout
    return {k: int(v) for k, v in (l.split("=") for l in out.split())}

def scenarios(exe):
    r = {}
    # nominal: 320 kbps MP3 (40 KB/s) at the measured card speed (6 ms a 4 KB read = about 680 KB/s)
    r["nom"] = run(exe)
    # high rate: FLAC at 176 KB/s with the same card
    r["flac"] = run(exe, size=600000, rate=176000, frame=4096, lat=5600, jit=1500, maxms=20000)
    # odd cap that is not a multiple of the chunk, many seeds
    r["odd"] = [run(exe, size=100000 + 777 * s, cap=24576 - 100 * s, seed=s, jit=4000) for s in range(1, 6)]
    # unknown size: the end is found by failures
    r["unk"] = run(exe, known=0, size=150001)
    r["unk_exact"] = run(exe, known=0, size=151552)           # an exact multiple of the chunk
    # transient failures right after an open (slot settling), known and unknown size
    r["settle_k"] = run(exe, settle=2)
    r["settle_u"] = run(exe, known=0, settle=2, size=150001)
    # the 1.07 s slot-settle outlier on the 3rd read (B-740): a consumer at 40 KB/s with a 24 KB ring drains in 600 ms -> starves, but must recover without a bad byte
    r["outlier"] = run(exe, outl=3, outl_ms=1074)
    # a seek in the middle with a command in flight
    r["seek"] = [run(exe, size=300000, seek_ms=ms, seek_to=100000 + ms, seed=ms, lat=9000, jit=3000) for ms in range(200, 2200, 37)]   # scanned so that some seeks land with a command in flight
    # a command that never finishes: timeout counted, nothing else issued
    r["hang"] = run(exe, outl=2, outl_ms=7000, maxms=9000)
    return r

def properties(r):
    ok = {}
    def allr(f): return all(f(x) for x in (r["odd"] + r["seek"] + [r[k] for k in r if isinstance(r[k], dict)]))
    ok["every byte exact (all scenarios)"] = allr(lambda x: x["bad"] == 0)
    ok["never two commands in flight"] = allr(lambda x: x["started_busy"] == 0)
    ok["no read wraps the ring"] = allr(lambda x: x["wrap"] == 0)
    n = r["nom"]; ok["nominal MP3 completes, no starvation"] = n["done"] == 1 and n["consumed"] == 200000 and n["starved"] == 0
    f = r["flac"]; ok["FLAC rate completes, no starvation"] = f["done"] == 1 and f["consumed"] == 600000 and f["starved"] == 0
    ok["read-ahead keeps a lead (nominal steady min above 2 frames)"] = n["steady_min"] >= 2 * 1152
    ok["odd ring sizes complete"] = all(x["done"] == 1 and x["consumed"] == 100000 + 777 * s for s, x in zip(range(1, 6), r["odd"]))
    u = r["unk"]; ok["unknown size: end found; only the sub-minimum tail (<512 B) is unreachable"] = u["done"] in (1, 2) and 150001 - u["consumed"] < 512 and u["size_final"] == u["pos"]
    u = r["unk_exact"]; ok["unknown size, exact chunk multiple"] = u["done"] == 1 and u["consumed"] == 151552
    for k in ("settle_k", "settle_u"):
        x = r[k]; ok[f"{k}: transient open failures survived"] = x["done"] in ((1,) if k == "settle_k" else (1, 2)) and x["retries"] >= 2 and x["err"] == 0 and (k == "settle_k" or 150001 - x["consumed"] < 512)
    o = r["outlier"]; ok["1.07 s outlier: counted starvation, recovers, exact"] = o["starved"] >= 1 and o["done"] == 1 and o["consumed"] == 200000 and o["max_lat"] >= 1074000 // 1000 * 1000 // 1000
    ok["seek: stale completions dropped, never accepted"] = all(x["bad"] == 0 and x["seeks"] == 1 and x["done"] == 1 for x in r["seek"]) and sum(x["stale"] for x in r["seek"]) >= 1
    h = r["hang"]; ok["hang: timeout counted, nothing issued meanwhile"] = h["timeouts"] == 1 and h["started_busy"] == 0
    return ok

with tempfile.TemporaryDirectory() as td:
    d = Path(td)
    good = properties(scenarios(build(d, 0)))
    for k, v in good.items(): check(k, v)
    for bug, name in ((1, "no generation check"), (2, "read wraps the ring"), (3, "no read-ahead"), (4, "second command while one is in flight"), (5, "first failure is end of file")):
        bad = properties(scenarios(build(d, bug)))
        check(f"mutant {bug} ({name}) is caught", any(not v for v in bad.values()))
print("FAILED" if fails else "all ok", fails)
sys.exit(1 if fails else 0)
