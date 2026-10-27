#!/usr/bin/env python3
"""gfx_scroll_down and gfx_scroll_left (fw2/gfx/scroll.c) against a plain reference, with the double buffer modelled the way the hardware addresses it: two drawing buffers and ONE shared stash. The scroll is ONE pass of the
engine's copy inside the drawing buffer (no temporary, B-901; sim/tb_tau_gfx_move.v shows on the RTL that this is exact for a left move in any overlap and for a down move in chunks of at most d rows from the bottom). For random
boxes (several chunks tall and wide), either buffer as the drawing target: every pixel of the box equals the reference shift, everything else of the buffer is untouched, the OTHER buffer is untouched, the stash is not touched
at all, no copy overlaps itself vertically (a copy to other rows must not overlap its own rows) or moves right, and none is wider than 127. Mutants: down chunks top-down; down chunks taller than d; left chunks right to left."""
import random, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
C = r'''
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../fw2/gfx/scroll.c"
#define STRIDE 512u
#define N (1024u * STRIDE)
static uint16_t buf[2][N], stash[N], ref[N], bufsave[2][N], stashsave[N]; static uint32_t cur; static int overlap, wide, plain, stash_used;
static void copy(uint16_t *src, uint16_t *dst, uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h)
{
    if (w > 127u) wide = 1;
    if (src == dst) {
        const int rows_overlap = !(dy + h <= sy || sy + h <= dy), cols_overlap = !(dx + w <= sx || sx + w <= dx);
        if (rows_overlap && dy != sy) overlap = 1;                       /* a copy to other rows must not overlap its own rows: the engine reads a row's destination as a later row's source */
        if (rows_overlap && dy == sy && cols_overlap && dx > sx) overlap = 1;   /* in the same rows only a move to the left is safe in the modelled order */
    }
    for (uint32_t j = 0; j < h; j++) for (uint32_t i = 0; i < w; i++) dst[(dy + j) * STRIDE + dx + i] = src[(sy + j) * STRIDE + sx + i];
}
/* the engine copies as the hardware addresses them: through the drawing buffer's base; a copy through the stash would be a failure now */
void gfx_blit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) { plain = 1; copy(buf[cur], buf[cur], sx, sy, dx, dy, w, h); }
void gfx_blit_to_stash(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) { stash_used = 1; copy(buf[cur], stash, sx, sy, dx, dy, w, h); }
void gfx_blit_stash(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) { stash_used = 1; copy(stash, buf[cur], sx, sy, dx, dy, w, h); }
int main(int argc, char **argv)
{
    srand((unsigned)atoi(argv[1])); const int left = argc > 2;
    for (int t = 0; t < 40; t++) {
        const uint32_t w = 1u + (uint32_t)rand() % 400u, h = 2u + (uint32_t)rand() % 358u, x = (uint32_t)rand() % (401u - w), y = (uint32_t)rand() % (361u - h), d = 1u + (uint32_t)rand() % 12u;
        cur = (uint32_t)rand() & 1u;
        for (uint32_t i = 0; i < N; i++) { buf[0][i] = (uint16_t)(rand() * 31u); buf[1][i] = (uint16_t)(rand() * 17u); stash[i] = (uint16_t)(rand() * 13u); bufsave[0][i] = buf[0][i]; bufsave[1][i] = buf[1][i]; stashsave[i] = stash[i]; ref[i] = buf[cur][i]; }
        memcpy(ref, buf[cur], sizeof ref);
        if (left) { for (uint32_t j = 0; j < h; j++) for (uint32_t i = 0; i + d < w; i++) ref[(y + j) * STRIDE + x + i] = buf[cur][(y + j) * STRIDE + x + i + d]; }
        else if (d < h) for (uint32_t j = h - 1u; j >= d; j--) { for (uint32_t i = 0; i < w; i++) ref[(y + j) * STRIDE + x + i] = buf[cur][(y + j - d) * STRIDE + x + i]; if (j == d) break; }
        overlap = wide = plain = stash_used = 0;
        if (left) gfx_scroll_left(x, y, w, h, d); else gfx_scroll_down(x, y, w, h, d);
        uint32_t bad = 0;
        for (uint32_t j = 0; j < 1024u; j++) for (uint32_t i = 0; i < STRIDE; i++) {
            const uint32_t k = j * STRIDE + i;
            if (buf[cur][k] != ref[k]) bad++;                                              /* the box shifted, everything else of the drawing buffer untouched */
            if (buf[cur ^ 1u][k] != bufsave[cur ^ 1u][k]) bad++;                         /* the other buffer untouched */
            if (stash[k] != stashsave[k]) bad++;                                           /* the stash not touched at all */
        }
        printf("%u %d %d %d\n", bad, overlap, wide, plain + 2 * stash_used);
    }
    return 0;
}
'''
def run(mutant, seed, left=False):
    with tempfile.TemporaryDirectory() as td:
        d = Path(td); (d / "fw2/gfx").mkdir(parents=True); (d / "sim").mkdir()
        src = (ROOT / "fw2/gfx/scroll.c").read_text()
        if mutant == "topdown":
            a = "for (uint32_t rem = h - d; rem; ) {\n        const uint32_t n = rem < d ? rem : d, ys = y + rem - n;"; assert a in src
            src = src.replace(a, "for (uint32_t rem = h - d, done = 0; rem; ) {\n        const uint32_t n = rem < d ? rem : d, ys = y + done;").replace("        rem -= n;", "        rem -= n; done += n;")
        if mutant == "tall":
            a = "const uint32_t n = rem < d ? rem : d, ys"; assert a in src
            src = src.replace(a, "const uint32_t n = rem < 2u * d ? rem : 2u * d, ys")
        if mutant == "rtl":
            a = "for (uint32_t xo = d; xo < w; xo += SCROLL_COLS) gfx_blit(x + xo, y, x + xo - d, y, w - xo < SCROLL_COLS ? w - xo : SCROLL_COLS, h);"; assert a in src
            src = src.replace(a, "for (uint32_t xo = ((w - d - 1u) / SCROLL_COLS) * SCROLL_COLS + d; ; xo -= SCROLL_COLS) { gfx_blit(x + xo, y, x + xo - d, y, w - xo < SCROLL_COLS ? w - xo : SCROLL_COLS, h); if (xo == d) break; }")
        (d / "fw2/gfx/scroll.c").write_text(src); (d / "fw2/gfx/gfx.h").write_text((ROOT / "fw2/gfx/gfx.h").read_text()); (d / "sim/h.c").write_text(C)
        r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True)
        if r.returncode: print(r.stderr); sys.exit(1)
        return [tuple(int(v) for v in ln.split()) for ln in subprocess.run([str(d / "h"), str(seed)] + (["left"] if left else []), capture_output=True, text=True).stdout.splitlines()]
fails = 0
def check(n, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + n); fails += 0 if ok else 1
res = [r for s in (1, 2, 3) for r in run(None, s)]
check("gfx_scroll_down: every pixel equals the reference shift in either drawing buffer, the other buffer and the stash untouched (%d random boxes, up to 358 rows and 400 columns)" % len(res), all(r[0] == 0 for r in res))
check("gfx_scroll_down: one pass in the drawing buffer: no copy overlaps itself, none is wider than 127, none goes through the stash", all(r[1] == 0 and r[2] == 0 and r[3] < 2 for r in res))
resl = [r for s in (1, 2, 3) for r in run(None, s, True)]
check("gfx_scroll_left: every pixel equals the reference shift left (%d random boxes), one pass in the drawing buffer, none wider than 127, none through the stash" % len(resl), all(r[0] == 0 and r[1] == 0 and r[2] == 0 and r[3] < 2 for r in resl))
badl = [r for s in (1, 2, 3) for r in run("rtl", s, True)]
check("mutant (left scroll chunks right to left) is caught", any(r[0] != 0 for r in badl))
for m, name in (("topdown", "down chunks top-down"), ("tall", "down chunks taller than d")):
    bad = [r for s in (1, 2, 3) for r in run(m, s)]
    check("mutant (%s) is caught" % name, any(r[0] != 0 or r[1] != 0 for r in bad))
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
