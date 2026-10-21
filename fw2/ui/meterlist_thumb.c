#pragma GCC optimize ("Os")
#include "meterlist_thumb.h"
#include "../gfx/gfx.h"
#include "theme.h"
#include "../cores/ramp_core.h"
#include "../generated/meter_registry.h"
#include "../generated/meter_thumbs.h"

/* The METER list previews: each meter's 56x32 picture (meters/<key>/thumb.json) as an index plane in the off-screen stash, built once from its run-length stream (one rectangle per run), then drawn with the
 * palette blit through that meter's own 8-colour table, whose entries follow the active theme (a role, or a ramp between two roles; a fixed colour where the manifest gives none). Cold code. */
#define THUMB_PLANE_Y 740u                       /* stash rows 740..771: the meters side by side from x = 0 (5 x 56 columns); the art plane is at 400, the icons at 560, the scope strip at 600, Chladni at 984 */
static uint8_t built[METER_N];
static void build(uint32_t m)
{
    uint32_t x = 0, y = 0;
    for (uint32_t i = mth_off[m]; i < mth_off[m + 1u]; i++) {
        uint32_t run = (uint32_t)(mth_rle[i] & 31u) + 1u; const uint16_t idx = (uint16_t)(mth_rle[i] >> 5);
        while (run) { const uint32_t n = run < MTH_W - x ? run : MTH_W - x; gfx_rect_stash(m * MTH_W + x, THUMB_PLANE_Y + y, n, 1, idx); run -= n; x += n; if (x == MTH_W) { x = 0; y++; } }
    }
    built[m] = 1;
}
void meter_thumb_draw(uint32_t m, uint32_t x, uint32_t y)
{
    if (m >= METER_N) return;
    if (!built[m]) build(m);
    gfx_clut_begin(0);
    for (uint32_t c = 0; c < 8u; c++) {
        const mth_col_t *e = &mth_pal[m][c];
        gfx_clut_put(e->a == 255u ? e->fixed : e->n ? rc_ramp(th_role[e->a], th_role[e->b], e->t, e->n) : th_role[e->a]);
    }
    gfx_cblit(m * MTH_W, THUMB_PLANE_Y, x, y, MTH_W, MTH_H);
}
