#!/usr/bin/env python3
"""Host test for fw/headroom.h (Cymo C0a, B-538): the worst-second tracker (settling, reset) and the projected maximum speed."""
import subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include "%s"
static int bad;
static void chk(int c, const char *m) { if (!c) { printf("FAIL %%s\n", m); bad++; } else printf("ok   %%s\n", m); }
int main(void)
{
    chk(hr_max_speed_x100(60, 1, 1, 0) == 250u, "60%% idle at 1.0x: 2.5x possible (busy 40%%)");
    chk(hr_max_speed_x100(50, 1, 1, 0) == 200u, "50%% idle at 1.0x: 2.0x");
    chk(hr_max_speed_x100(50, 6, 5, 0) == 240u, "50%% idle while running at 1.2x: busy per 1x is 41.7%%, so 2.4x");
    chk(hr_max_speed_x100(50, 1, 1, 16) == 168u, "the same with a 16%% fixed extra load (stretcher): 1.68x");
    chk(hr_max_speed_x100(99, 1, 1, 0) == 10000u, "99%% idle reads very high, no divide by zero");
    chk(hr_max_speed_x100(100, 1, 1, 0) == 10000u, "100%% idle is clamped to 99%%");
    chk(hr_max_speed_x100(0, 1, 1, 0) == 100u, "0%% idle at 1.0x: exactly 1.0x");
    chk(hr_max_speed_x100(50, 1, 1, 100) == 0u, "an extra load of 100%% leaves nothing");
    chk(hr_max_speed_x100(70, 17, 20, 0) == 283u, "70%% idle at 0.85x");
    hr_t h; hr_reset(&h);
    chk(h.min_idle == 100u && h.secs == 0u && h.last == 255u && h.max_io == 0u, "reset: no history");
    hr_update(&h, 5, 90); hr_update(&h, 5, 90);
    chk(h.min_idle == 100u && h.last == 255u, "the first HR_SETTLE_SECS seconds are ignored (loading is not steady state)");
    hr_update(&h, 70, 3); hr_update(&h, 40, 12); hr_update(&h, 80, 5);
    chk(h.min_idle == 40u, "afterwards the worst (lowest idle) second is kept");
    chk(h.last == 80u, "and the latest counted second is remembered");
    chk(h.max_io == 12u, "the worst file-wait second is kept, settling seconds excluded");
    hr_reset(&h); hr_update(&h, 0, 0);
    chk(h.min_idle == 100u, "reset starts the settling again");
    for (int i = 0; i < 400; i++) hr_update(&h, 50, 7);
    chk(h.secs == 255u && h.min_idle == 50u, "the second counter saturates");
    { ur_t u = {0, 0, 0};
      ur_note(&u, 0, 1); ur_note(&u, 0, 1); ur_note(&u, 0, 0);
      chk(u.n == 0, "empty before the FIFO was ever seen full is the prefill, not an underrun");
      ur_note(&u, 1, 0); ur_note(&u, 0, 0);
      chk(u.n == 0, "full then draining is normal");
      ur_note(&u, 0, 1); ur_note(&u, 0, 1); ur_note(&u, 0, 1);
      chk(u.n == 1, "a stall spanning several pushes counts once");
      ur_note(&u, 0, 0); ur_note(&u, 0, 1);
      chk(u.n == 2, "empty again after refilling counts as a second stall");
      ur_flush(&u); ur_note(&u, 0, 1); ur_note(&u, 0, 1);
      chk(u.n == 2, "after a flush the prefill is ignored again, the total is kept");
    }
    return bad != 0;
}
'''
def main():
    with tempfile.TemporaryDirectory() as d:
        src = Path(d) / "t.c"; exe = Path(d) / "t"
        src.write_text(HARNESS % str(ROOT / "fw/headroom.h"))
        subprocess.run(["cc", "-O1", "-Wall", "-Werror", "-o", str(exe), str(src)], check=True)
        r = subprocess.run([str(exe)], capture_output=True, text=True)
        sys.stdout.write(r.stdout)
        if r.returncode: sys.exit(1)
    print("PASSED")
if __name__ == "__main__": main()
