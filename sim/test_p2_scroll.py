#!/usr/bin/env python3
"""gfx_scroll_down (fw2/gfx/scroll.c) against a plain reference: a copy engine that refuses overlapping copies (like the hardware's) and a framebuffer; for random boxes (including heights that need several chunks and
widths that need several column chunks) every pixel of the box equals the reference shift, the top d rows are untouched and nothing outside the box except the stash temporary changed. Mutant: the chunks run top-down."""
import random, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
C = r'''
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "../fw2/gfx/scroll.c"
#define STRIDE 512u
static uint16_t fb[1024u * STRIDE]; static int overlap, wide;
void gfx_blit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h)
{
    if (w > 127u) wide = 1;
    if (!(dx + w <= sx || sx + w <= dx || dy + h <= sy || sy + h <= dy)) overlap = 1;      /* the engine's copy must not overlap itself */
    for (uint32_t j = 0; j < h; j++) for (uint32_t i = 0; i < w; i++) fb[(dy + j) * STRIDE + dx + i] = fb[(sy + j) * STRIDE + sx + i];
}
int main(int argc, char **argv)
{
    srand((unsigned)atoi(argv[1]));
    for (int t = 0; t < 40; t++) {
        const uint32_t w = 1u + (uint32_t)rand() % 400u, h = 2u + (uint32_t)rand() % 358u, x = (uint32_t)rand() % (401u - w), y = (uint32_t)rand() % (361u - h), d = 1u + (uint32_t)rand() % 12u;
        for (uint32_t i = 0; i < 1024u * STRIDE; i++) fb[i] = (uint16_t)(rand() * 31u);
        static uint16_t ref[1024u * STRIDE]; for (uint32_t i = 0; i < 1024u * STRIDE; i++) ref[i] = fb[i];
        if (d < h) for (uint32_t j = h - 1u; j >= d; j--) { for (uint32_t i = 0; i < w; i++) ref[(y + j) * STRIDE + x + i] = fb[(y + j - d) * STRIDE + x + i]; if (j == d) break; }
        overlap = wide = 0;
        gfx_scroll_down(x, y, w, h, d);
        uint32_t bad = 0;
        for (uint32_t j = 0; j < 1024u; j++) for (uint32_t i = 0; i < STRIDE; i++) {
            const int temp = j >= 780u && j < 980u && i < 400u;                      /* the stash temporary may hold anything */
            if (!temp && fb[j * STRIDE + i] != ref[j * STRIDE + i]) bad++;
        }
        printf("%u %d %d\n", bad, overlap, wide);
    }
    return 0;
}
'''
def run(mutant, seed):
    with tempfile.TemporaryDirectory() as td:
        d = Path(td); (d / "fw2/gfx").mkdir(parents=True); (d / "sim").mkdir()
        src = (ROOT / "fw2/gfx/scroll.c").read_text()
        if mutant:
            a = "for (uint32_t rem = h - d; rem; ) {\n        const uint32_t n = rem < SCROLL_TEMP_ROWS ? rem : SCROLL_TEMP_ROWS, ys = y + rem - n;"; assert a in src
            src = src.replace(a, "for (uint32_t rem = h - d, done = 0; rem; ) {\n        const uint32_t n = rem < SCROLL_TEMP_ROWS ? rem : SCROLL_TEMP_ROWS, ys = y + done;").replace("        rem -= n;", "        rem -= n; done += n;")
        (d / "fw2/gfx/scroll.c").write_text(src); (d / "fw2/gfx/gfx.h").write_text((ROOT / "fw2/gfx/gfx.h").read_text()); (d / "sim/h.c").write_text(C)
        r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True)
        if r.returncode: print(r.stderr); sys.exit(1)
        return [tuple(int(v) for v in ln.split()) for ln in subprocess.run([str(d / "h"), str(seed)], capture_output=True, text=True).stdout.splitlines()]
fails = 0
def check(n, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + n); fails += 0 if ok else 1
res = [r for s in (1, 2, 3) for r in run(False, s)]
check("every pixel equals the reference shift (%d random boxes, up to 358 rows and 400 columns)" % len(res), all(r[0] == 0 for r in res))
check("no engine copy overlaps itself and none is wider than 127", all(r[1] == 0 and r[2] == 0 for r in res))
bad = [r for s in (1, 2, 3) for r in run(True, s)]
check("mutant (chunks top-down) is caught", any(r[0] != 0 for r in bad))
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
