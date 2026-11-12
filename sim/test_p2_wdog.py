#!/usr/bin/env python3
"""Pack watchdog, firmware side (docs/features/PACK_WATCHDOG_PLAN.md, W2): the pure rule in fw2/cores/pack_strike_core.h (the hardware window wd_limit) and the call site in
fw2/ui/meters.c (a tick the watchdog cut off strikes the pack out at once and forces a repaint: tested with the window loader, sim/test_p2_meter_window.py). The hardware path (trap entry, recovery, the registers) is proven separately on the real CPU:
`make test-rtl-wdog-soc`. Mutant: no overflow guard in the window."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/player2"))
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("  " + str(info) if info and not ok else ""))
    fails += 0 if ok else 1

CORE = r'''
#include <stdio.h>
#include <stdint.h>
#include "pack_strike_core.h"
int main(void)
{
    const uint32_t soft[] = {0u, 1u, 200000u, 500000u, 600000u, 5000000u, 0xFFFFFFFFu, 0x40000000u};
    for (unsigned i = 0; i < sizeof soft / sizeof *soft; i++) printf("%u\n", wd_limit(soft[i]));
    return 0;
}
'''
def core(d, flags=()):
    d.mkdir(parents=True, exist_ok=True)
    (d / "c.c").write_text(CORE)
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", *flags, "-I", str(ROOT / "fw2/cores"), "-o", str(d / "c"), str(d / "c.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return subprocess.run([str(d / "c")], capture_output=True, text=True).stdout.strip().splitlines()

with tempfile.TemporaryDirectory() as td:
    tmp = Path(td)
    o = core(tmp / "core")
    want = [20000000, 2000000, 2000000, 2000000, 2400000, 20000000, 20000000, 20000000]
    check("hardware window = 4 x the soft limit, floor 2 M, ceiling 20 M, 0 (no limit) = ceiling", [int(x) for x in o[:8]] == want, o[:8])
    check("mutant (no overflow guard in the window) is caught", [int(x) for x in core(tmp / "m4", ["-DBUG=4"])[:8]] != want)

    # the call site (a cut tick strikes the pack out at once, from the next picture Winamp Bars draws, the cut ignored is a mutant) moved with the loader: sim/test_p2_meter_window.py, built with -DWD_HOST_CUT
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
