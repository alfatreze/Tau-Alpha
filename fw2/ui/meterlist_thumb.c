#pragma GCC optimize ("Os")
#include "meterlist_thumb.h"
#include "../gfx/gfx.h"
#include "theme.h"
#include "../cores/ramp_core.h"
#include "../generated/meter_registry.h"
#include "../generated/meter_thumbs.h"
#include "meter.h"

/* The METER list previews: each meter's 56x32 picture (meters/<key>/thumb.json) as an index plane in the off-screen stash, built once from its run-length stream (one rectangle per run), then drawn with the
 * palette blit through that meter's own 8-colour table, whose entries follow the active theme (a role, or a ramp between two roles; a fixed colour where the manifest gives none). Cold code. */
#define THUMB_PLANE_Y 740u                       /* stash rows 740..771: the meters side by side from x = 0 (5 x 56 columns); the art plane is at 400, the icons at 560, the scope strip at 600, Chladni at 984 */
static uint8_t built[METER_SLOTS];
static void run_out(uint32_t m, uint32_t *x, uint32_t *y, uint32_t byte)      /* one run of the stream: `run` pixels of palette index byte >> 5, wrapped at the row end, one rectangle each */
{
    uint32_t run = (byte & 31u) + 1u; const uint16_t idx = (uint16_t)(byte >> 5);
    while (run) { const uint32_t n = run < MTH_W - *x ? run : MTH_W - *x; gfx_rect_stash(m * MTH_W + *x, THUMB_PLANE_Y + *y, n, 1, idx); run -= n; *x += n; if (*x == MTH_W) { *x = 0; (*y)++; } }
}
static void build(uint32_t m)
{
    uint32_t x = 0, y = 0;
    if (m < METER_N) for (uint32_t i = mth_off[m]; i < mth_off[m + 1u]; i++) run_out(m, &x, &y, mth_rle[i]);
    else for (uint32_t i = 0, n = meters_thumb_len(m); i < n; i++) { uint8_t v; if (!meters_thumb_rle(m, i, &v)) break; run_out(m, &x, &y, v); }       /* a pack-only meter: its stream is in its descriptor */
    built[m] = 1;
}
static uint16_t colour(uint16_t fixed, uint32_t a, uint32_t b, uint32_t t, uint32_t n)
{
    return a == 255u ? fixed : n ? rc_ramp(th_role[a], th_role[b], t, n) : th_role[a];
}
void meter_thumb_draw(uint32_t m, uint32_t x, uint32_t y)
{
    if (m >= METER_SLOTS || !meters_present(m)) return;
    if (!built[m]) build(m);
    gfx_clut_begin(0);
    for (uint32_t c = 0; c < 8u; c++) {
        uint16_t fixed = 0; uint8_t a = 255, b = 0, t = 0, n = 0;
        if (m < METER_N) { const mth_col_t *e = &mth_pal[m][c]; fixed = e->fixed; a = e->a; b = e->b; t = e->t; n = e->n; }
        else (void)meters_thumb_col(m, c, &fixed, &a, &b, &t, &n);
        gfx_clut_put(colour(fixed, a, b, t, n));
    }
    gfx_cblit(m * MTH_W, THUMB_PLANE_Y, x, y, MTH_W, MTH_H);
}
