/* fw2/cores/cellgrid_core.h -- the geometry and the state store of a grid of square cells with a one pixel gap (the Waterfall; any meter made of cells). Pure: no drawing. Host-tested (sim/test_p2_meter_helpers.py).
 *
 * cg_fit() fits as many whole cells as the box holds (capped at the state store's size), and centres the grid: what does not fit a whole cell is split between the sides, so the grid fills the box at any cell size.
 * The state store is a NIBBLE a cell (0..15: a brightness step, a heat): a 66 x 53 grid (the fullscreen box at the smallest cell) is 1,749 bytes. The state of cell (c, r) is at index r * stride + c. */
#ifndef CELLGRID_CORE_H
#define CELLGRID_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
typedef struct { uint16_t x0, y0; uint8_t cols, rows, pitch, cell; } cg_geom_t;       /* x0, y0: the top-left of the grid; pitch = cell + 1 */
#define CG_BYTES(cols, rows) ((((cols) * (rows)) + 1u) / 2u)                              /* the state store of a grid at most cols x rows cells */
/* Fits the grid into the box (bx, by, bw, bh) for cells of `cell` pixels, at most maxc x maxr cells. Returns 0 when not even one cell fits. */
static inline int cg_fit(cg_geom_t *g, uint32_t bx, uint32_t by, uint32_t bw, uint32_t bh, uint32_t cell, uint32_t maxc, uint32_t maxr)
{
    const uint32_t pitch = cell + 1u;
    uint32_t cols = bw / pitch, rows = bh / pitch;
    if (cols > maxc) cols = maxc;
    if (rows > maxr) rows = maxr;
    g->cols = (uint8_t)cols; g->rows = (uint8_t)rows; g->pitch = (uint8_t)pitch; g->cell = (uint8_t)cell;
    if (!cols || !rows) { g->x0 = (uint16_t)bx; g->y0 = (uint16_t)by; return 0; }
#if BUG == 1
    g->x0 = (uint16_t)bx; g->y0 = (uint16_t)by;                                          /* not centred: the leftover is all on the right and bottom */
#else
    g->x0 = (uint16_t)(bx + (bw - cols * pitch) / 2u); g->y0 = (uint16_t)(by + (bh - rows * pitch) / 2u);
#endif
    return 1;
}
static inline uint32_t cg_x(const cg_geom_t *g, uint32_t c) { return g->x0 + c * g->pitch; }          /* the left edge of column c */
static inline uint32_t cg_y(const cg_geom_t *g, uint32_t r) { return g->y0 + r * g->pitch; }          /* the top edge of row r */
static inline uint32_t cg_w(const cg_geom_t *g) { return (uint32_t)g->cols * g->pitch; }              /* the grid's width and height (the last cell's gap included) */
static inline uint32_t cg_h(const cg_geom_t *g) { return (uint32_t)g->rows * g->pitch; }
/* A signature that changes when the box, the cell size or anything else that moves the grid changes (compare it to the last one: a new geometry is a full repaint). */
static inline uint32_t cg_sig(uint32_t bw, uint32_t bh, uint32_t cell, uint32_t extra) { return (bw << 20) ^ (bh << 10) ^ (cell << 4) ^ extra; }
/* The nibble store. */
static inline uint32_t cg_get(const uint8_t *z, uint32_t i) { return (z[i >> 1] >> ((i & 1u) * 4u)) & 15u; }
static inline void cg_set(uint8_t *z, uint32_t i, uint32_t v)
{
    const uint32_t sh = (i & 1u) * 4u;
#if BUG == 2
    z[i >> 1] = (uint8_t)(v << sh);                                                       /* clobbers the neighbour nibble */
#else
    z[i >> 1] = (uint8_t)((z[i >> 1] & ~(15u << sh)) | ((v & 15u) << sh));
#endif
}
static inline void cg_zero(uint8_t *z, uint32_t bytes) { for (uint32_t i = 0; i < bytes; i++) z[i] = 0; }
#endif
