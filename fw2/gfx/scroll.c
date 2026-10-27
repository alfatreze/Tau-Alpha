#pragma GCC optimize ("Os")
#include "gfx.h"

/* gfx_scroll_down / gfx_scroll_left (fw2/gfx/gfx.h): the box moves by d rows / columns inside the drawing buffer in ONE pass of the engine's copy, with no temporary (the earlier version went through a
 * stash strip and copied the whole box twice: measured on a Pocket as 95% of a fullscreen Waterfall tick, 634 k of 661 k cycles, docs/research/TALOS_T3B_RESULT_2026-10-27.md). What the engine does with a copy
 * (sim/tb_tau_gfx_move.v, on the RTL): every row is read whole into its row buffer and then written, rows top to bottom. So
 *  - a move LEFT (destination in the same rows, d columns to the left) is exact in one command per column chunk, whatever the overlap: chunks of at most 120 columns (the copy takes at most 127 words a row),
 *    LEFT TO RIGHT, because a chunk's destination only covers columns that were already moved (or the d columns that fall off the box): the next chunk's source starts d columns to the right of where this one's
 *    destination ends;
 *  - a move DOWN cannot be one command (a row's destination is read as a source later, the result is not a move): chunks of at most d rows, BOTTOM UP, so a chunk's destination rows never overlap its own source rows
 *    and only cover rows that were already moved (or the d rows that fall off the box). A small d means many short commands (the CPU issues them while the engine copies; the meter's command ceiling is 3,072). */
#define SCROLL_COLS 120u
void gfx_scroll_down(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t d)
{
    if (!w || !d || d >= h) return;
    for (uint32_t rem = h - d; rem; ) {
        const uint32_t n = rem < d ? rem : d, ys = y + rem - n;
        for (uint32_t xo = 0; xo < w; xo += SCROLL_COLS) gfx_blit(x + xo, ys, x + xo, ys + d, w - xo < SCROLL_COLS ? w - xo : SCROLL_COLS, n);
        rem -= n;
    }
}
void gfx_scroll_left(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t d)
{
    if (!h || !d || d >= w) return;
    for (uint32_t xo = d; xo < w; xo += SCROLL_COLS) gfx_blit(x + xo, y, x + xo - d, y, w - xo < SCROLL_COLS ? w - xo : SCROLL_COLS, h);
}
