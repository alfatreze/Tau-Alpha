/* fw2/ui/meter_grid.h -- drawing a grid of cells (the Waterfall, any meter made of cells): header-only so it uses whatever gfx_rect the including file has (a pack's is redirected to the host table).
 * The geometry and the state store are fw2/cores/cellgrid_core.h. Include after meter.h, theme.h, gfx.h and ramp_core.h (and, in a pack, after the gfx_rect redirect). */
#ifndef METER_GRID_H
#define METER_GRID_H
#include "../cores/cellgrid_core.h"
typedef struct { cg_geom_t g; uint16_t pal[4]; } mgrid_t;      /* the geometry and the four brightness steps (0 = the dim cell that keeps the grid visible, 3 = the accent) */
/* The four steps from the background to the accent (the same ramp the Waterfall always used). */
static inline void mgrid_palette(mgrid_t *m, uint16_t bg, uint16_t accent)
{
    m->pal[0] = rc_ramp(bg, accent, 1u, 7u); m->pal[1] = rc_ramp(bg, accent, 3u, 7u); m->pal[2] = rc_ramp(bg, accent, 5u, 7u); m->pal[3] = accent;
}
static inline void mgrid_cell(const mgrid_t *m, uint32_t c, uint32_t r, uint16_t colour) { gfx_rect(cg_x(&m->g, c), cg_y(&m->g, r), m->g.cell, m->g.cell, colour); }
/* The whole box in the background colour and every cell dim (step 0), the state store cleared: a new context or geometry. */
static inline void mgrid_clear(const mgrid_t *m, uint8_t *state, uint32_t state_bytes, uint32_t bx, uint32_t by, uint32_t bw, uint32_t bh, uint16_t bg)
{
    gfx_rect(bx, by, bw, bh, bg);
    cg_zero(state, state_bytes);
    for (uint32_t r = 0; r < m->g.rows; r++) for (uint32_t c = 0; c < m->g.cols; c++) mgrid_cell(m, c, r, m->pal[0]);
}
/* Cell (c, r) takes brightness `step` (0..3): drawn only when it differs from what is drawn. `stride` is the state store's row length (its column cap). Returns 1 when it drew. */
static inline int mgrid_put(const mgrid_t *m, uint8_t *state, uint32_t stride, uint32_t c, uint32_t r, uint32_t step)
{
    const uint32_t i = r * stride + c;
    if (cg_get(state, i) == step) return 0;
    cg_set(state, i, step); mgrid_cell(m, c, r, m->pal[step & 3u]);
    return 1;
}
#endif
