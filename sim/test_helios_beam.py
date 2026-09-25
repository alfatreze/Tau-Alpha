#!/usr/bin/env python3
"""Host test for fw/helios.inc's beam-aware safety rule (helios_rows_safe): the real C, compiled on the host with a fake
R_SCAN register, checked exhaustively against an independent statement of the rule (docs/HELIOS_SPEC.md, B-267)."""
import itertools, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
static uint32_t fake_scan;
#define R_SCAN   0x800000E8u
#define R_VBLANK 0x800000D0u
#define REG(a) (*(volatile uint32_t *)((a) == R_SCAN ? &fake_scan : &fake_scan))
#include "%s"
int main(void) {
    /* argv-free: emit the full truth table  beam_ok vc y0 y1 -> safe  for a grid of inputs */
    for (int ok = 0; ok < 2; ok++) {
        helios_beam_ok = (uint8_t)ok;
        for (uint32_t vc = 0; vc < 400; vc++) {
            fake_scan = 0x200u | vc;
            for (uint32_t y0 = 0; y0 < 360; y0 += 7)
                for (uint32_t h = 0; h < 120; h += 5) {
                    uint32_t y1 = y0 + h; if (y1 > 359) continue;
                    printf("%%d %%u %%u %%u %%d\n", ok, vc, y0, y1, helios_rows_safe(y0, y1));
                }
        }
    }
    return 0;
}
'''

def model(ok, vc, y0, y1):
    """The rule, stated independently: blanking / already passed / safely ahead."""
    if not ok: return 1
    if vc < 4 or vc >= 364: return 1
    by = vc - 4
    if y1 <= by + 1: return 1
    if y0 >= by + 2 + 3: return 1
    return 0

with tempfile.TemporaryDirectory() as td:
    src = Path(td) / "t.c"
    src.write_text(HARNESS % str(ROOT / "fw/helios.inc"))
    exe = Path(td) / "t"
    r = subprocess.run(["cc", "-std=c11", "-Wall", "-Wno-unused-function", "-o", str(exe), str(src)], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    out = subprocess.check_output([str(exe)], text=True).splitlines()

bad = 0
for line in out:
    ok, vc, y0, y1, got = map(int, line.split())
    if got != model(ok, vc, y0, y1):
        bad += 1
        if bad < 5: print("MISMATCH", ok, vc, y0, y1, got)
n = len(out)
# spot guarantees that matter most
def safe(vc, y0, y1): return model(1, vc, y0, y1)
checks = [
    ("beam inside the region is unsafe", safe(4 + 130, 126, 259) == 0),
    ("region above the beam is safe", safe(4 + 300, 126, 259) == 1),
    ("region well below the beam is safe", safe(4 + 50, 126, 259) == 1),
    ("region just below the beam (inside the lead) is unsafe", safe(4 + 122, 126, 259) == 0),
    ("vertical blanking is always safe", all(safe(vc, 0, 359) for vc in list(range(0, 4)) + list(range(364, 400)))),
    ("no beam information means always safe", all(model(0, vc, 0, 359) for vc in range(400))),
]
for name, cond in checks:
    print(("ok   " if cond else "FAIL ") + name)
    bad += 0 if cond else 1
print(f"{n} table entries compared against the model")
print("PASSED" if not bad else f"FAILED ({bad})")
sys.exit(1 if bad else 0)
