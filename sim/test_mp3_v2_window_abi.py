#!/usr/bin/env python3
"""MP3 v2 WP-02: the window unit's ABI (docs/features/MP3_WINDOW_UNIT_ABI_2026-11-25.md) holds: word[P] = round(2^23 * X[m(P)]) per slot makes the unit's golden
model (and the RTL under iverilog) equal the standard's float64 synthesis x 32768, rounded half up and clipped (tools/mp3_v2/window_abi_check.py).
Red: each mutant of the ABI (scale 2^22, scale 2^24, natural order instead of the push order, sign of P17..P31, history one slot late) must fail a named check."""
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/mp3_v2")); sys.path.insert(0, str(ROOT / "tools"))
import window_abi_check as A
fails = 0
def check(name, ok, detail=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + detail))
    if not ok: fails += 1

base = A.run_checks(None)
for n, ok, d in base: check(n, ok, d)
check("green run has no skipped model checks (at least 17 checks ran)", len(base) >= 17, str(len(base)))
for mut in ("scale_low", "scale_high", "order", "sign", "age"):
    r = A.run_checks(mut, with_rtl=False)
    failed = [n for n, ok, _ in r if not ok]
    check(f"red: mutant '{mut}' is caught ({len(failed)} check(s) fail)", any("model PCM" in n for n in failed), str(failed))
sys.exit(1 if fails else 0)
