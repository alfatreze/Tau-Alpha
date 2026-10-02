#!/usr/bin/env python3
"""Layered Wave's history ring (fw/layered_wave.inc, B-509) against the shift register it replaced.

The golden-frame test proves the DRAWING is unchanged. This one proves the STORAGE directly, and far harder than any drawing scenario can: it drives the
real lw_reinit()/lw_push()/lw_unwrap() with random push sequences several times longer than the ring (so the head wraps many times, the unwrapped arc
straddles the wrap at every alignment, and the layer count and resolution change between runs), and after every batch compares each layer's unwrapped
samples with an independent copy of the ORIGINAL shift-register algorithm. The meter framework's rule for any meter that keeps state in PSRAM
(docs/features/meters/METER_MODULE_SPEC.md section 27.3 rule 7) is: a ring test like this one, plus golden frames, plus a hardware label.

A final sensitivity check compares the ring with the reference shifted by one sample and requires it to FAIL, so a comparison that cannot detect an
off-by-one cannot pass this test.
"""
import subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define COLD_FN3
#define COLD_DATA
#define WAVE_COLS 64u
#include "theme.h"
#include "meter_gen_enum.h"
#include "meter_module.h"
#include "meter.h"
#include "meters_gen.h"
static uint16_t ui_accent;
static uint8_t paused, wviz_force;
static void fb_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
#include "layered_wave.inc"

static uint32_t rng = 0x12345678u;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* the ORIGINAL design, verbatim: shift up to res+3 bytes per layer per push, newest at index 0 */
static uint8_t ref[LW_MAXL][LW_HMAX];
static void ref_push(int32_t n, int32_t res) {
    for (int32_t k = 0; k < n; k++) {
        uint8_t *h = ref[k];
        for (int32_t j = res + 3; j > 0; j--) h[j] = h[j - 1];
        h[0] = (uint8_t)lw_u8(lw_s[k]);
    }
}

int main(void) {
    long compared = 0, bad = 0, off_by_one_diffs = 0, pushes_total = 0, head_wraps = 0;
    const int32_t ress[] = { 16, 24, 100, 200, 256, 392, 396, 397, 398, 399, 400 };   /* res + 4 up to the whole ring, every alignment near it */
    for (int run = 0; run < 400; run++) {
        const int32_t n = 1 + (int32_t)(rnd() % LW_MAXL);
        const int32_t res = (run < 11 * 4) ? ress[run % 11] : 16 + (int32_t)(rnd() % 385u);
        lw_p_t p; memset(&p, 0, sizeof p); p.layers = (uint8_t)n;
        const int32_t key[6] = { n, res, 400, 60, 0, 0 }, soft[13] = { 0 };
        lw_reinit(&p, 400, key, soft);
        memset(ref, 0, sizeof ref);
        /* a hot lw_row full of junk first: lw_unwrap must overwrite every sample it claims to return */
        memset(lw_row, 0xA5, sizeof lw_row);
        const int batches = 60 + (int)(rnd() % 80u);
        uint16_t prev_head = lw_head;
        for (int b = 0; b < batches; b++) {
            const int np = (int)(rnd() % 10u);                         /* 0 pushes is a real case: a redraw with no new data */
            for (int i = 0; i < np; i++) {
                for (int k = 0; k < n; k++) lw_s[k] = (int32_t)(rnd() % 4096u);   /* lw_u8() clamps/scales these, so values span 0..255 */
                lw_push(n); ref_push(n, res); pushes_total++;
                if (lw_head > prev_head) head_wraps++;                  /* head moves DOWN; an increase is a wrap */
                prev_head = lw_head;
            }
            for (int k = 0; k < n; k++) {
                memset(lw_row, 0xA5, sizeof lw_row);
                lw_unwrap(k, (uint32_t)(res + 4));
                for (int32_t j = 0; j < res + 4; j++) {
                    compared++;
                    if (lw_row[j] != ref[k][j]) { if (bad < 5) printf("DIFF run %d batch %d layer %d j %d ring %u ref %u (n=%d res=%d head=%u)\n", run, b, k, j, lw_row[j], ref[k][j], n, res, lw_head); bad++; }
                    if (j + 1 < res + 4 && lw_row[j] != ref[k][j + 1]) off_by_one_diffs++;   /* sensitivity: an off-by-one SHOULD differ */
                }
            }
        }
    }
    printf("compared %ld samples over %ld pushes (%ld head wraps): %ld differences; off-by-one control differs in %ld places\n", compared, pushes_total, head_wraps, bad, off_by_one_diffs);
    if (bad) return 1;
    if (head_wraps < 100) { printf("test too weak: only %ld head wraps\n", head_wraps); return 1; }
    if (off_by_one_diffs < compared / 4) { printf("test too weak: the off-by-one control barely differs, so the comparison could not catch it\n"); return 1; }
    return 0;
}
'''


def main():
    with tempfile.TemporaryDirectory() as d:
        d = Path(d)
        (d / "h.c").write_text(HARNESS)
        r = subprocess.run(["cc", "-O1", "-Wall", "-DTAU_DIAGNOSTIC=1", "-Wno-unused-function", "-Wno-unused-variable", "-Wno-unused-but-set-variable",
                            "-I", str(ROOT / "fw"), "-o", str(d / "h"), str(d / "h.c")], capture_output=True, text=True)
        if r.returncode:
            print(r.stderr); sys.exit(1)
        out = subprocess.run([str(d / "h")], capture_output=True, text=True)
    print(out.stdout.strip())
    if out.returncode:
        print("lw ring test FAILED"); sys.exit(1)
    print("lw ring OK: the PSRAM ring equals the shift register it replaced")


main()
