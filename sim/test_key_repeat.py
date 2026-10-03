#!/usr/bin/env python3
"""Host test for fw/key_repeat.h (B-534): a held key repeats after the hold delay at the period, a tap never repeats, releasing stops it, the repeat flag is
only set on repeat calls, two keys held repeat together, and a cycle-counter wrap is harmless."""
import subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "%s"
#define UP 1u
#define DOWN 2u
#define HOLD 400u
#define PER 80u
static int bad;
static void chk(int c, const char *m) { if (!c) { printf("FAIL %%s\n", m); bad++; } else printf("ok   %%s\n", m); }
/* simulate: key `keys` pressed at t=0 (edge on first call) held until `until`; poll every 10 ticks; count extra edges and note first repeat time */
static int run(uint32_t start, uint32_t keys, uint32_t until, uint32_t *first)
{
    kr_t k = {0}; int n = 0; *first = 0;
    for (uint32_t t = 0; t <= until + 200u; t += 10u) {
        const uint32_t now = start + t;
        const uint32_t held = t <= until ? keys : 0u;
        const uint32_t edge = t == 0 ? keys : 0u;
        uint32_t rep;
        const uint32_t e = kr_step(&k, edge, held, UP | DOWN, now, HOLD, PER, &rep);
        if (rep) { if (!n) *first = t; n++; if (!(e & keys)) bad++; }
        if (!held && e) bad++;
    }
    return n;
}
int main(void)
{
    uint32_t first;
    { int n = run(1000u, UP, 300u, &first); chk(n == 0, "a tap shorter than the hold delay never repeats"); }
    { int n = run(1000u, UP, 1000u, &first); chk(first >= HOLD && first < HOLD + 20u, "first repeat arrives just after the hold delay");
      chk(n >= 7 && n <= 8, "then one repeat per period (about 7 in the next 600 ms)"); }
    { int n = run(1000u, UP, 500u, &first); chk(n >= 1 && n <= 2, "released shortly after the delay: only the repeats before release"); }
    { int n = run(1000u, UP | DOWN, 800u, &first); chk(n >= 4, "two keys held repeat together"); }
    { int n = run(0xFFFFFF00u, UP, 1000u, &first); chk(first >= HOLD && first < HOLD + 20u && n >= 7, "a cycle counter wrapping mid-hold is harmless"); }
    { kr_t k = {0}; uint32_t rep = 9; (void)kr_step(&k, 0, 0, UP, 5000u, HOLD, PER, &rep); chk(rep == 0, "nothing held, no repeat"); }
    return bad != 0;
}
'''
def main():
    with tempfile.TemporaryDirectory() as d:
        src = Path(d) / "t.c"; exe = Path(d) / "t"
        src.write_text(HARNESS % str(ROOT / "fw/key_repeat.h"))
        subprocess.run(["cc", "-O1", "-Wall", "-Werror", "-o", str(exe), str(src)], check=True)
        r = subprocess.run([str(exe)], capture_output=True, text=True)
        sys.stdout.write(r.stdout)
        if r.returncode: sys.exit(1)
    print("PASSED")
if __name__ == "__main__": main()
