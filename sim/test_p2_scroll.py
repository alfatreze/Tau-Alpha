#!/usr/bin/env python3
"""gfx_scroll_down (fw2/gfx/scroll.c) against a plain reference with the double buffer modelled the way the hardware addresses it: two drawing buffers and ONE shared stash; a copy inside the drawing buffer, a copy
from the buffer to the stash and one from the stash to the buffer. For random boxes (several chunks tall and wide), either buffer as the drawing target: every pixel of the box equals the reference shift, the top d
rows are untouched, the OTHER buffer is untouched, the stash changed only in its temporary rows, no engine copy overlaps itself, none is wider than 127. Mutants: chunks top-down; the temporary kept in the drawing
buffer's own rows (the first version: wrong with the double buffer)."""
import random, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
C = r'''
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "../fw2/gfx/scroll.c"
#define STRIDE 512u
#define N (1024u * STRIDE)
static uint16_t buf[2][N], stash[N], ref[N], bufsave[2][N], stashsave[N]; static uint32_t cur; static int overlap, wide, plain;
static void copy(uint16_t *src, uint16_t *dst, uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h)
{
    if (w > 127u) wide = 1;
    if (src == dst && !(dx + w <= sx || sx + w <= dx || dy + h <= sy || sy + h <= dy)) overlap = 1;      /* the engine's copy must not overlap itself */
    for (uint32_t j = 0; j < h; j++) for (uint32_t i = 0; i < w; i++) dst[(dy + j) * STRIDE + dx + i] = src[(sy + j) * STRIDE + sx + i];
}
/* the three engine copies as the hardware addresses them: through the drawing buffer's base, to the shared stash, from the shared stash */
void gfx_blit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) { plain = 1; copy(buf[cur], buf[cur], sx, sy, dx, dy, w, h); }
void gfx_blit_to_stash(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) { copy(buf[cur], stash, sx, sy, dx, dy, w, h); }
void gfx_blit_stash(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) { copy(stash, buf[cur], sx, sy, dx, dy, w, h); }
int main(int argc, char **argv)
{
    srand((unsigned)atoi(argv[1]));
    for (int t = 0; t < 40; t++) {
        const uint32_t w = 1u + (uint32_t)rand() % 400u, h = 2u + (uint32_t)rand() % 358u, x = (uint32_t)rand() % (401u - w), y = (uint32_t)rand() % (361u - h), d = 1u + (uint32_t)rand() % 12u;
        cur = (uint32_t)rand() & 1u;
        for (uint32_t i = 0; i < N; i++) { buf[0][i] = (uint16_t)(rand() * 31u); buf[1][i] = (uint16_t)(rand() * 17u); stash[i] = (uint16_t)(rand() * 13u); bufsave[0][i] = buf[0][i]; bufsave[1][i] = buf[1][i]; stashsave[i] = stash[i]; ref[i] = buf[cur][i]; }
        if (d < h) for (uint32_t j = h - 1u; j >= d; j--) { for (uint32_t i = 0; i < w; i++) ref[(y + j) * STRIDE + x + i] = buf[cur][(y + j - d) * STRIDE + x + i]; if (j == d) break; }
        overlap = wide = plain = 0;
        gfx_scroll_down(x, y, w, h, d);
        uint32_t bad = 0;
        for (uint32_t j = 0; j < 1024u; j++) for (uint32_t i = 0; i < STRIDE; i++) {
            const uint32_t k = j * STRIDE + i;
            if (buf[cur][k] != ref[k]) bad++;                                              /* the box shifted, everything else of the drawing buffer untouched */
            if (buf[cur ^ 1u][k] != bufsave[cur ^ 1u][k]) bad++;                         /* the other buffer untouched */
            const int temp = j >= 780u && j < 980u && i < 400u;
            if (!temp && stash[k] != stashsave[k]) bad++;                                  /* the stash changed only in the temporary */
        }
        printf("%u %d %d %d\n", bad, overlap, wide, plain);
    }
    return 0;
}
'''
def run(mutant, seed):
    with tempfile.TemporaryDirectory() as td:
        d = Path(td); (d / "fw2/gfx").mkdir(parents=True); (d / "sim").mkdir()
        src = (ROOT / "fw2/gfx/scroll.c").read_text()
        if mutant == "topdown":
            a = "for (uint32_t rem = h - d; rem; ) {\n        const uint32_t n = rem < SCROLL_TEMP_ROWS ? rem : SCROLL_TEMP_ROWS, ys = y + rem - n;"; assert a in src
            src = src.replace(a, "for (uint32_t rem = h - d, done = 0; rem; ) {\n        const uint32_t n = rem < SCROLL_TEMP_ROWS ? rem : SCROLL_TEMP_ROWS, ys = y + done;").replace("        rem -= n;", "        rem -= n; done += n;")
        if mutant == "ownbase":
            a = "gfx_blit_to_stash(x + xo, ys, xo, SCROLL_TEMP_Y, ww, n);"; b = "gfx_blit_stash(xo, SCROLL_TEMP_Y, x + xo, ys + d, ww, n);"; assert a in src and b in src
            src = src.replace(a, "gfx_blit(x + xo, ys, xo, SCROLL_TEMP_Y, ww, n);").replace(b, "gfx_blit(xo, SCROLL_TEMP_Y, x + xo, ys + d, ww, n);")
        (d / "fw2/gfx/scroll.c").write_text(src); (d / "fw2/gfx/gfx.h").write_text((ROOT / "fw2/gfx/gfx.h").read_text()); (d / "sim/h.c").write_text(C)
        r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True)
        if r.returncode: print(r.stderr); sys.exit(1)
        return [tuple(int(v) for v in ln.split()) for ln in subprocess.run([str(d / "h"), str(seed)], capture_output=True, text=True).stdout.splitlines()]
fails = 0
def check(n, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + n); fails += 0 if ok else 1
res = [r for s in (1, 2, 3) for r in run(None, s)]
check("every pixel equals the reference shift in either drawing buffer, the other buffer and the stash untouched (%d random boxes, up to 358 rows and 400 columns)" % len(res), all(r[0] == 0 for r in res))
check("no engine copy overlaps itself, none is wider than 127, and no copy goes through the drawing buffer's own base to the temporary", all(r[1] == 0 and r[2] == 0 and r[3] == 0 for r in res))
for m, name in (("topdown", "chunks top-down"), ("ownbase", "the temporary in the drawing buffer's own rows")):
    bad = [r for s in (1, 2, 3) for r in run(m, s)]
    check("mutant (%s) is caught" % name, any(r[0] != 0 or r[3] != 0 for r in bad))
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
