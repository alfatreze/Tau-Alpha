#pragma GCC optimize ("Os")
#include "gfx.h"

/* gfx_scroll_down (fw2/gfx/gfx.h): the box moves down by d rows through the SHARED stash temporary (rows 780..979, columns 0..399: read and written through base 0, not through the drawing buffer's base,
 * so the temporary is the same stash in both buffers of the double buffer), because a copy inside the buffer must not overlap itself. From the bottom up, in
 * chunks of at most 200 rows and 120 columns (the engine's copy takes at most 127 words a row): copy a chunk out to the temporary, then back d rows lower. A chunk's destination only covers rows that were
 * already moved (or the d rows that fall off the box), so no source is overwritten before it is read. The engine runs in order: no fence is needed between the copies. */
#define SCROLL_TEMP_Y 780u
#define SCROLL_TEMP_ROWS 200u
#define SCROLL_COLS 120u
void gfx_scroll_down(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t d)
{
    if (!w || !d || d >= h) return;
    for (uint32_t rem = h - d; rem; ) {
        const uint32_t n = rem < SCROLL_TEMP_ROWS ? rem : SCROLL_TEMP_ROWS, ys = y + rem - n;
        for (uint32_t xo = 0; xo < w; xo += SCROLL_COLS) { const uint32_t ww = w - xo < SCROLL_COLS ? w - xo : SCROLL_COLS; gfx_blit_to_stash(x + xo, ys, xo, SCROLL_TEMP_Y, ww, n); }
        for (uint32_t xo = 0; xo < w; xo += SCROLL_COLS) { const uint32_t ww = w - xo < SCROLL_COLS ? w - xo : SCROLL_COLS; gfx_blit_stash(xo, SCROLL_TEMP_Y, x + xo, ys + d, ww, n); }
        rem -= n;
    }
}

/* gfx_scroll_left: the box moves left by d columns. In chunks of at most 120 columns and 200 rows, LEFT TO RIGHT: a chunk's destination only covers columns that were already moved (or the d columns that fall off
 * the box), because the next chunk's source starts d columns to the right of where this one's destination ends. Copy a chunk out to the temporary, then back d columns to the left. */
void gfx_scroll_left(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t d)
{
    if (!h || !d || d >= w) return;
    for (uint32_t yo = 0; yo < h; yo += SCROLL_TEMP_ROWS) {
        const uint32_t n = h - yo < SCROLL_TEMP_ROWS ? h - yo : SCROLL_TEMP_ROWS;
        for (uint32_t xo = d; xo < w; xo += SCROLL_COLS) {
            const uint32_t ww = w - xo < SCROLL_COLS ? w - xo : SCROLL_COLS;
            gfx_blit_to_stash(x + xo, y + yo, 0u, SCROLL_TEMP_Y, ww, n);
            gfx_blit_stash(0u, SCROLL_TEMP_Y, x + xo - d, y + yo, ww, n);
        }
    }
}
