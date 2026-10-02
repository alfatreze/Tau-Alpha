#!/usr/bin/env python3
"""Host test for fw/meter_policy.h (B-525): the throttle for the heavy full-repaint meter. Checks the draw/skip pattern, that skipped time is carried to the next
draw, that a forced repaint always draws, that dt never exceeds MP_MAX_DT_MS (Layered Wave's scroll arithmetic overflows int32 above ~87 ms), that no time is
invented, and that the cycle counter wrapping is harmless."""
import subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "%s"
#define MS 66667u
static int bad;
static void chk(int c, const char *m) { if (!c) { printf("FAIL %%s\n", m); bad++; } else printf("ok   %%s\n", m); }
int main(void) {
    const uint32_t MIN = 45u * MS;
    { /* 26 ms ticks: draw, skip, draw (dt carries), skip, draw */
        mp_t m = {0}; uint32_t dt = 0; char pat[16]; uint32_t dts[8]; int n = 0;
        for (int i = 0; i < 6; i++) {
            const uint32_t now = (uint32_t)i * 26u * MS;
            const int d = mp_throttle(&m, now, MIN, 26u, 0, 1, &dt);
            pat[i] = d ? 'D' : 's'; if (d) { dts[n++] = dt; mp_drew(&m, now); }
        }
        pat[6] = 0;
        chk(!strcmp(pat, "DsDsDs"), "26 ms ticks against a 45 ms minimum: draw every other call");
        chk(n == 3 && dts[0] == 26u && dts[1] == 52u && dts[2] == 52u, "the skipped 26 ms is carried into the next draw (26, 52, 52)");
    }
    { /* a low FIFO skips, and the banked time is clamped */
        mp_t m = {0}; uint32_t dt = 0; mp_throttle(&m, 0, MIN, 26u, 0, 1, &dt); mp_drew(&m, 0);
        int skipped = 0;
        for (int i = 1; i <= 100; i++) skipped += !mp_throttle(&m, (uint32_t)i * 26u * MS, MIN, 26u, 0, 0, &dt);
        chk(skipped == 100, "FIFO low: every call skipped");
        const int d = mp_throttle(&m, 101u * 26u * MS, MIN, 26u, 0, 1, &dt);
        chk(d && dt == MP_MAX_DT_MS, "after a 2.6 s yield the delivered dt is clamped to MP_MAX_DT_MS, not 2.6 s");
        chk((int64_t)MP_MAX_DT_MS * 240 * 400 * 256 < 2147483647LL, "MP_MAX_DT_MS keeps dt*speed*res*256 inside int32");
        chk((uint64_t)MP_MAX_DT_MS * 16777216u < 4294967296ULL, "MP_MAX_DT_MS keeps dt*2^24 inside uint32");
    }
    { /* force always draws, even too soon and with the FIFO low */
        mp_t m = {0}; uint32_t dt = 0; mp_throttle(&m, 0, MIN, 26u, 0, 1, &dt); mp_drew(&m, 0);
        chk(mp_throttle(&m, 1u, MIN, 26u, 1, 0, &dt) && dt == 26u, "a forced repaint draws immediately with the plain dt");
    }
    { /* wrap-around of the cycle counter */
        mp_t m = {0}; uint32_t dt = 0; const uint32_t t0 = 0xFFFFFFFFu - 10u * MS;
        mp_throttle(&m, t0, MIN, 26u, 0, 1, &dt); mp_drew(&m, t0);
        chk(!mp_throttle(&m, t0 + 26u * MS, MIN, 26u, 0, 1, &dt), "26 ms after a draw across the counter wrap: still skipped");
        chk(mp_throttle(&m, t0 + 52u * MS, MIN, 26u, 0, 1, &dt), "52 ms after a draw across the counter wrap: draws");
    }
    { /* random: never more than MAX, never more time delivered than elapsed */
        mp_t m = {0}; uint32_t dt = 0, now = 0, maxdt = 0; uint64_t delivered = 0, elapsed = 0; srand(7);
        for (int i = 0; i < 20000; i++) {
            const uint32_t step = 5u + (uint32_t)(rand() %% 60);
            now += step * MS; elapsed += step;      /* now wraps (32-bit cycles); elapsed does not */
            const int force = (rand() %% 50) == 0, aff = (rand() %% 5) != 0;
            if (mp_throttle(&m, now, MIN, step, force, aff, &dt)) { delivered += dt; if (dt > maxdt) maxdt = dt; mp_drew(&m, now); }
        }
        chk(maxdt <= MP_MAX_DT_MS, "random run: dt never above the clamp");
        chk(delivered <= elapsed, "random run: no more time delivered than elapsed");
    }
    printf(bad ? "FAILED %%d\n" : "PASSED\n", bad);
    return bad != 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    src = Path(td) / "t.c"
    src.write_text("#include <string.h>\n" + HARNESS % str(ROOT / "fw/meter_policy.h"))
    exe = Path(td) / "t"
    r = subprocess.run(["cc", "-std=c11", "-Wall", "-Wno-unused-function", "-o", str(exe), str(src)], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    p = subprocess.run([str(exe)], capture_output=True, text=True)
    print(p.stdout.strip()); sys.exit(p.returncode)
