#!/usr/bin/env python3
"""Player 2 output rate (fw2/aout/aout_rate.h, used by aout.c rate_apply): the output tick must equal the SOURCE rate for every supported rate (the I2S is fixed at 48 kHz and hold-resamples),
times the varispeed ratio, clamped to 8-48 kHz. Before B-1171 every rate except 44.1 kHz drained at 48 kHz (32 kHz played 1.5x fast)."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
CLK = 66666667
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    if not ok: fails += 1
with tempfile.TemporaryDirectory() as td:
    exe = Path(td) / "h"
    r = subprocess.run(["cc", "-O1", "-Wall", "-Werror", "-I", str(ROOT / "fw2"), "-o", str(exe), str(ROOT / "sim/p2_aout_rate_host.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    def inc(*args): return [int(x) for x in subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True).stdout.split()]
    rates = [8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000]
    got = inc(*[a for hz in rates for a in (hz, 1, 1)])
    for hz, g in zip(rates, got):
        want = (hz << 32) // CLK
        check(f"{hz} Hz drains at {hz} Hz (increment {g}, want {want})", g == want)
    check("32 kHz is not 48 kHz", inc(32000, 1, 1)[0] != inc(48000, 1, 1)[0])
    for hz in (32000, 44100, 22050):
        base = inc(hz, 1, 1)[0]; fast = inc(hz, 3, 2)[0]; slow = inc(hz, 3, 4)[0]
        check(f"varispeed multiplies the source tick at {hz} Hz (1.5x, 0.75x)", abs(fast - base * 1.5) <= 1 and abs(slow - base * 0.75) <= 1)
    lo, hi, below, above = inc(8000, 1, 1)[0], inc(48000, 1, 1)[0], inc(1000, 1, 1)[0], inc(96000, 1, 1)[0]
    check("rates outside 8-48 kHz are clamped (1 kHz -> 8 kHz, 96 kHz -> 48 kHz)", below == lo and above == hi)
print("PASS" if not fails else f"{fails} FAILED")
sys.exit(1 if fails else 0)
