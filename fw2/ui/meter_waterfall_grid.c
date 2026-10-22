#pragma GCC optimize ("Os")
#include "meter.h"
#include "theme.h"
#include "../gfx/gfx.h"
#include "../generated/meter_presets.h"
#include "../cores/ramp_core.h"
#include "../cores/meter_core.h"

/* Waterfall (meters/waterfall_grid/meter.json): a grid of small cells lit by the spectrum. The grid is ALWAYS visible: an unlit cell is a dim cell, never the background.
 * SPECTROGRAM: frequency across, time down. The grid scrolls smoothly, a few pixels every picture at a set pixel speed (so the speed does not depend on the cell size), by the engine's copy (gfx_scroll_down, a
 * handful of commands); the next row enters from the top edge (its cells grow into view like the rest of the grid moves), its brightness the band's level in three steps with a small ordered dither. The entering row is
 * built ONCE in the stash as a plane of palette indices (the four steps and the background; a rectangle a cell, once a row) and shown with a few palette blits a picture (gfx_cblit), not a rectangle a cell a picture.
 * ZONES: the grid keeps still, in 4 x 4 regions (bass top-left to treble bottom-right in reading order); each region is one band and fills from its centre outwards as the level rises (a heat map).
 * DUST: the same still grid with the bands scattered at random over the cells (the first version).
 * SPARKLE: the still grid; a band that jumps well above its own running average (an onset: a hit, a pluck) lights a burst of cells around its column (the louder the jump, the wider the burst and the more cells),
 * which fade back to the dim grid in three steps over the Fade time. Silence and sustained notes light nothing. Only the flashes in a short list are visited and drawn, so the cost follows the events, not the grid.
 * Compose is the host's (manifest p2: composed only in SPECTROGRAM, so the scroll happens in the idle buffer and is never seen tearing). The standard degrade: the host skips windows by mtr_in_t.degrade; the still
 * modes also refresh fewer rows (mtr_degrade_div).
 *
 * A pack-only meter (the framework test, docs/features/meters/METER_FRAMEWORK_FINDINGS.md): the real code exists only with P2_PACK (meter_pack_waterfall_grid.c); the firmware carries a stub that tells the
 * viewer the pack is missing, so a build without the pack costs a few dozen bytes, not the meter. */
#define WF_COLS 48u
#define WF_ROWS 40u
typedef struct { uint8_t mode, gain, cell; uint16_t speed, fade; } wf_cfg_t;
static wf_cfg_t cfg = { 0u, 100u, 8u, 90u, 700u };
/* wf_load: the live parameters (the manifest's order) into the working configuration; a pack calls it each tick with the host's table. */
static void wf_load(const uint16_t *v) { cfg.mode = (uint8_t)v[MPP_WF_MODE]; cfg.speed = v[MPP_WF_SPEED]; cfg.gain = (uint8_t)v[MPP_WF_GAIN]; cfg.cell = (uint8_t)v[MPP_WF_CELL]; cfg.fade = v[MPP_WF_FADE]; }

#ifdef P2_PACK
static uint8_t zone[WF_ROWS * WF_COLS];                  /* the still modes: the brightness step (0..3) drawn in each cell */
static uint8_t rowv[WF_COLS];                            /* SPECTROGRAM: the steps of the row that is entering */
#define WF_ACT 192u
static uint16_t act[WF_ACT], band_avg[MTR_BANDS];        /* SPARKLE: the cells that are lit (grid index; their heat is in zone[]); each band's running average of its level (x16) */
static uint32_t nact;
static uint32_t acc_q8, expo, step_n, geom;              /* scroll pixels not yet taken (1/256); how much of the entering row is in view; rows entered (the dither and the dust change with it); the geometry drawn for */
static uint32_t hx(uint32_t a, uint32_t b)
{
    uint32_t x = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA6Bu; x ^= x >> 15; x *= 0x2C1B3C6Du; x ^= x >> 12; return x;
}
static uint32_t bayer(uint32_t x, uint32_t y)            /* ordered dither 0..15 */
{
    static const uint8_t b4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
    return b4[(y & 3u) * 4u + (x & 3u)];
}
static uint32_t cell_step(uint32_t lv, uint32_t thr)     /* 0 off (the dim cell), 1..3 brighter the further the level is above the cell's threshold */
{
    if (lv <= thr) return 0u;
    const uint32_t t = (lv - thr) * 3u / (256u - thr);
    return 1u + (t > 2u ? 2u : t);
}
static uint32_t col_level(const uint8_t *spec, uint32_t c, uint32_t cols)      /* the 16 bands spread over the columns, interpolated */
{
    if (cols < 2u) return spec[0];
    const uint32_t pos = c * 15u * 16u / (cols - 1u), b = pos >> 4, f = pos & 15u, b2 = b < 15u ? b + 1u : 15u;
    return ((uint32_t)spec[b] * (16u - f) + (uint32_t)spec[b2] * f) >> 4;
}
static uint32_t region_thr(uint32_t c, uint32_t r, uint32_t cols, uint32_t rows)   /* ZONES: the threshold grows from the centre of the cell's 4 x 4 region outwards (plus a little dither) */
{
    const uint32_t rc = c * 4u / cols, rr = r * 4u / rows, c0 = rc * cols / 4u, c1 = (rc + 1u) * cols / 4u, r0 = rr * rows / 4u, r1 = (rr + 1u) * rows / 4u;
    const int32_t dx = (int32_t)(2u * c) - (int32_t)(c0 + c1 - 1u), dy = (int32_t)(2u * r) - (int32_t)(r0 + r1 - 1u);
    const uint32_t ax = (uint32_t)(dx < 0 ? -dx : dx), ay = (uint32_t)(dy < 0 ? -dy : dy), sx = c1 - c0 > 1u ? c1 - c0 - 1u : 1u, sy = r1 - r0 > 1u ? r1 - r0 - 1u : 1u;
    const uint32_t nx = ax * 255u / sx, ny = ay * 255u / sy, hi = nx > ny ? nx : ny, lo = nx > ny ? ny : nx, d = (3u * hi + lo) / 4u;       /* an octagon: 0 at the centre, 255 at the edge */
    return d * 190u / 255u + bayer(c, r) * 3u;                                            /* 0..235 */
}
static uint32_t region_band(uint32_t c, uint32_t r, uint32_t cols, uint32_t rows) { return (r * 4u / rows) * 4u + c * 4u / cols; }
#define WF_PLANE_Y 722u                                   /* the entering row's plane in the stash (gfx.h map): pitch rows of palette indices, 0..3 the steps, 4 the background */
static void plane_build(uint32_t cols, uint32_t pitch, uint32_t gw)
{
    gfx_rect_stash(0u, WF_PLANE_Y, gw, pitch, 4u);
    for (uint32_t c = 0; c < cols; c++) gfx_rect_stash(c * pitch, WF_PLANE_Y, cfg.cell, cfg.cell, rowv[c]);
}
static void plane_blit(const mtr_in_t *in, uint32_t gw, uint32_t sy, uint32_t dy, uint32_t h)      /* plane rows sy.. to the grid's rows dy.. (the engine's copy takes at most 127 columns) */
{
    for (uint32_t xo = 0; xo < gw; xo += 127u) gfx_cblit(xo, WF_PLANE_Y + sy, in->x + xo, in->y + dy, gw - xo < 127u ? gw - xo : 127u, h);
}
#define WF_ONSET 28u                                             /* how far above its running average a band must jump (levels of 256) to count as an onset */
static uint32_t heat_step(uint32_t h) { return h >= 170u ? 3u : h >= 90u ? 2u : h >= 30u ? 1u : 0u; }      /* a flash's brightness step from its heat */
static void draw_cell(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t col) { gfx_rect(x, y, w, h, col); }
static void wf_tick(const mtr_in_t *in)
{
    const uint32_t pitch = cfg.cell + 1u;
    uint32_t cols = in->w / pitch, rows = in->h / pitch;
    if (cols > WF_COLS) cols = WF_COLS;
    if (rows > WF_ROWS) rows = WF_ROWS;
    if (!cols || !rows) return;
    const uint16_t bg = in->bg, pal[4] = { rc_ramp(bg, UI_ACCENT, 1u, 7u), rc_ramp(bg, UI_ACCENT, 3u, 7u), rc_ramp(bg, UI_ACCENT, 5u, 7u), UI_ACCENT };
    uint8_t spec[MTR_BANDS];
    for (uint32_t b = 0; b < MTR_BANDS; b++) { const uint32_t v = in->paused ? 0u : (uint32_t)in->spec[b] * cfg.gain / 100u; spec[b] = (uint8_t)(v > 255u ? 255u : v); }
    const uint32_t sig = (in->w << 20) ^ (in->h << 10) ^ (pitch << 4) ^ cfg.mode, gw = cols * pitch;
    if (in->force || sig != geom) {                                     /* a new context or geometry: the whole grid, dim, then start again */
        geom = sig; acc_q8 = 0; expo = 0; nact = 0;
        gfx_rect(in->x, in->y, in->w, in->h, bg);
        for (uint32_t r = 0; r < rows; r++) for (uint32_t c = 0; c < cols; c++) { zone[r * WF_COLS + c] = 0; draw_cell(in->x + c * pitch, in->y + r * pitch, cfg.cell, cfg.cell, pal[0]); }
        for (uint32_t c = 0; c < cols; c++) rowv[c] = 0;
        plane_build(cols, pitch, gw);
    }
    if (cfg.mode == 0u) {                                               /* SPECTROGRAM */
        acc_q8 += (uint32_t)in->dt_ms * cfg.speed * 256u / 1000u;
        uint32_t d = acc_q8 >> 8; acc_q8 &= 255u;
        if (!d) return;
        if (d > pitch) d = pitch;                                       /* behind: the picture jumps one cell, never more */
        const uint16_t clut[5] = { pal[0], pal[1], pal[2], pal[3], bg };
        gfx_clut_load(0u, clut, 5u);
        if (rows * pitch > d) gfx_scroll_down(in->x, in->y, gw, rows * pitch, d);
        expo += d;
        if (expo >= pitch) {                                            /* the entering row is complete: show it whole (the plane holds it), build the next one */
            const uint32_t over = expo - pitch;
            plane_blit(in, gw, 0u, over, pitch);
            expo = over; step_n++;
            for (uint32_t c = 0; c < cols; c++) { const int32_t lv = (int32_t)col_level(spec, c, cols) + ((int32_t)bayer(c, step_n) - 8) * 2; rowv[c] = (uint8_t)(lv < 40 ? 0u : lv < 110 ? 1u : lv < 180 ? 2u : 3u); }
            plane_build(cols, pitch, gw);
        }
        if (expo) plane_blit(in, gw, pitch - expo, 0u, expo);          /* the part of the entering row that is in view: the plane's lower rows come in first (the gap, then the cells) */
    } else if (cfg.mode == 3u) {                                        /* SPARKLE */
        const uint32_t div = mtr_degrade_div(in->degrade), dec = (uint32_t)in->dt_ms * 255u / (cfg.fade < 100u ? 100u : cfg.fade) + 1u;
        for (uint32_t k = 0; k < nact; ) {                              /* fade what is lit: a cell is drawn only when its step changes */
            const uint32_t i = act[k], h = zone[i], nh = h > dec ? h - dec : 0u, os = heat_step(h), ns = heat_step(nh);
            zone[i] = (uint8_t)nh;
            if (os != ns) draw_cell(in->x + (i % WF_COLS) * pitch, in->y + (i / WF_COLS) * pitch, cfg.cell, cfg.cell, pal[ns]);
            if (nh) k++; else act[k] = act[--nact];
        }
        step_n++;
        for (uint32_t b = 0; b < MTR_BANDS; b++) {
            const uint32_t lv = (uint32_t)spec[b] << 4, av = band_avg[b];
            band_avg[b] = (uint16_t)(lv > av ? av + ((lv - av) >> 2) : av - ((av - lv) >> 4));        /* quick to follow a rise, slow to forget */
            if (lv < av + (WF_ONSET << 4)) continue;                    /* not a jump */
            band_avg[b] = (uint16_t)lv;                                 /* the average jumps up with it: a held note is one onset, only a further jump is another */
            const uint32_t s = (lv - av) >> 4, radius = 1u + s / 48u, n = (2u + s / 16u) / div, centre = b * (cols - 1u) / (MTR_BANDS - 1u);
            for (uint32_t k = 0; k < n; k++) {
                const uint32_t h = hx(step_n * 31u + b, k + 5u), dx = (h >> 8) % (2u * radius + 1u), c = centre + dx < radius ? 0u : centre + dx - radius, r = (h >> 16) % rows;
                const uint32_t i = r * WF_COLS + (c < cols ? c : cols - 1u);
                if (zone[i]) {                                          /* already lit: refresh it (brighter again if it had faded) */
                    if (heat_step(zone[i]) != 3u) draw_cell(in->x + (i % WF_COLS) * pitch, in->y + (i / WF_COLS) * pitch, cfg.cell, cfg.cell, pal[3]);
                    zone[i] = 255u; continue;
                }
                if (nact >= WF_ACT) continue;
                zone[i] = 255u; act[nact++] = (uint16_t)i;
                draw_cell(in->x + (i % WF_COLS) * pitch, in->y + (i / WF_COLS) * pitch, cfg.cell, cfg.cell, pal[3]);
            }
        }
    } else {                                                            /* ZONES and DUST: the still grid, only the cells that changed are redrawn */
        const uint32_t div = mtr_degrade_div(in->degrade);
        for (uint32_t r = 0; r < rows; r++) {
            if (div > 1u && (r % div) != (in->frame % div)) continue;
            for (uint32_t c = 0; c < cols; c++) {
                uint32_t v;
                if (cfg.mode == 1u) v = cell_step(spec[region_band(c, r, cols, rows)], region_thr(c, r, cols, rows));
                else { const uint32_t h = hx(c + 1u, r + 77u); v = cell_step(spec[(h >> 8) & 15u], h >> 24); }
                if (v != zone[r * WF_COLS + c]) { zone[r * WF_COLS + c] = (uint8_t)v; draw_cell(in->x + c * pitch, in->y + r * pitch, cfg.cell, cfg.cell, pal[v]); }
            }
        }
    }
}
#else
static void wf_note(const mtr_in_t *in) { if (in->force) gfx_text(in->x + 8u, in->y + in->h / 2u - 8u, "WATERFALL NEEDS ITS PACK", UI_RED, in->bg); }
#endif

#ifndef P2_PACK
const char *meter_waterfall_grid_preset_name(uint32_t i) { return mpp_wf_name[i < MPP_WF_PRESETS ? i : 0u]; }
uint16_t meter_waterfall_grid_mv[MPP_WF_N];                      /* the live parameters (the Configure page edits these); the drawing is in the pack */
void meter_waterfall_grid_apply(void) { wf_load(meter_waterfall_grid_mv); }
void meter_waterfall_grid_preset(uint32_t i) { for (uint32_t k = 0; k < MPP_WF_N; k++) meter_waterfall_grid_mv[k] = mpp_wf[i < MPP_WF_PRESETS ? i : 0u][k]; meter_waterfall_grid_apply(); }
void meter_waterfall_grid_tick(const mtr_in_t *in) { wf_note(in); }
#endif
