#!/usr/bin/env python3
"""fw2/cores/meter_cost_core.h (per-meter tick cost for the Talos 2 T4 gate) on the host: the maximum and the moving averages, the first tick setting them, saturation, and three mutants
(no smoothing, the maximum never rising, the stall not recorded)."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
C = r'''
#include <stdio.h>
#include "meter_cost_core.h"
static int fails;
static void chk(const char *n, int ok) { printf("%s %s\n", ok ? "ok  " : "FAIL", n); if (!ok) fails++; }
int main(void)
{
    mcost_t c = {0};
    mcost_add(&c, 10, 8000, 800, 0);
    chk("the first tick sets every average", c.cmd_avg16 == 160 && c.cyc_avg == 8000 && c.stall_avg == 800 && c.fence_avg == 0 && c.cmd_max == 10 && c.n == 1);
    mcost_add(&c, 2, 4000, 0, 800);
    chk("the next tick moves each average one eighth of the way", c.cmd_avg16 == 160 + (32 - 160) / 8 && c.cyc_avg == 8000 + (4000 - 8000) / 8 && c.stall_avg == 700 && c.fence_avg == 100);
    chk("the maximum keeps the largest tick", c.cmd_max == 10);
    mcost_add(&c, 300, 1, 1, 1);
    chk("a bigger tick raises the maximum", c.cmd_max == 300);
    mcost_add(&c, 100000, 1, 1, 1);
    chk("commands saturate at 4095", c.cmd_max == 4095);
    for (int i = 0; i < 200; i++) mcost_add(&c, 5, 1000, 10, 20);
    chk("steady input converges (within 8 of the value)", c.cyc_avg >= 1000 && c.cyc_avg < 1010 && c.stall_avg >= 10 && c.stall_avg < 20 && c.fence_avg >= 20 && c.fence_avg < 30 && c.cmd_avg16 >= 80 && c.cmd_avg16 < 90);
    chk("a decreasing average does not wrap", c.cyc_avg < 100000);
    return fails ? 1 : 0;
}
'''
def run(bug):
    with tempfile.TemporaryDirectory() as td:
        d = Path(td); (d / "t.c").write_text(C)
        r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(d / "t"), str(d / "t.c")], capture_output=True, text=True)
        if r.returncode: print(r.stderr); sys.exit(1)
        p = subprocess.run([str(d / "t")], capture_output=True, text=True); return p.stdout.strip().split("\n"), p.returncode
fails = 0
out, rc = run(0)
for l in out:
    print(l)
    if l.startswith("FAIL"): fails += 1
for bug, name in ((1, "no smoothing"), (2, "maximum never rises"), (3, "stall not recorded")):
    out, rc = run(bug); ok = rc != 0
    print(("ok   " if ok else "FAIL ") + f"mutant {name} is caught")
    if not ok: fails += 1
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
