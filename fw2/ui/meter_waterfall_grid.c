#pragma GCC optimize ("Os")
#include "meter.h"
#include "theme.h"
#include "../gfx/gfx.h"
#include "../generated/meter_presets.h"
#include "../cores/ramp_core.h"
#include "../cores/meter_core.h"
#include "meter_grid.h"                              /* the cell grid: geometry, nibble state, delta drawing (B-814) */

/* Waterfall (meters/waterfall_grid/meter.json): a grid of small cells lit by the spectrum. The grid is ALWAYS visible: an unlit cell is a dim cell, never the background.
 * SPECTROGRAM: frequency across, time down. The grid scrolls smoothly, a few pixels every picture at a set pixel speed (so the speed does not depend on the cell size), by the engine's copy (gfx_scroll_down, a
 * handful of commands); the next row enters from the top edge (its cells grow into view like the rest of the grid moves), its brightness the band's level in three steps with a small ordered dither. The entering row is
 * built ONCE in the stash as a plane of palette indices (the four steps and the background; a rectangle a cell, once a row) and shown with a few palette blits a picture (gfx_cblit), not a rectangle a cell a picture.
 * ZONES: the grid keeps still, in 4 x 4 regions (bass top-left to treble bottom-right in reading order); each region is one band and fills from its centre outwards as the level rises (a heat map).
 * DUST: the same still grid with the bands scattered at random over the cells (the first version).
 * SPARKLE: the still grid; a band that jumps well above its own running average (an onset: a hit, a pluck) lights a burst of cells around its column (the louder the jump, the wider the burst and the more cells),
 * which fade back to the dim grid in three steps over the Fade time. Silence and sustained notes light nothing. Only the flashes in a short list are visited and drawn (at most 24 new ones a picture), so the cost follows
 * the events, not the grid. The grid fills the box at any cell size (up to 66 x 53 cells) and is centred in it.
 * Compose is the host's (manifest p2: composed only in SPECTROGRAM, so the scroll happens in the idle buffer and is never seen tearing). The standard degrade: the host skips windows by mtr_in_t.degrade; the still
 * modes also refresh fewer rows (mtr_degrade_div).
 *
 * A pack-only meter (the framework test, docs/features/meters/METER_FRAMEWORK_FINDINGS.md): the real code exists only with P2_PACK (meter_pack_waterfall_grid.c); the firmware carries a stub that tells the
 * viewer the pack is missing, so a build without the pack costs a few dozen bytes, not the meter. */
#define WF_COLS 67u                                      /* the most the fullscreen box holds at the smallest cell (5 px + 1: 400 / 6 columns, 323 / 6 rows): the grid always fills the box */
#define WF_ROWS 54u
typedef struct { uint8_t mode, gain, cell; uint16_t speed, fade; } wf_cfg_t;
static wf_cfg_t cfg = { 0u, 100u, 8u, 90u, 450u };
/* wf_load: the live parameters (the manifest's order) into the working configuration; a pack calls it each tick with the host's table. */
static void wf_load(const uint16_t *v) { cfg.mode = (uint8_t)v[MPP_WF_MODE]; cfg.speed = v[MPP_WF_SPEED]; cfg.gain = (uint8_t)v[MPP_WF_GAIN]; cfg.cell = (uint8_t)v[MPP_WF_CELL]; cfg.fade = v[MPP_WF_FADE]; }

#ifdef P2_PACK
static uint8_t zone[CG_BYTES(WF_COLS, WF_ROWS)];     /* a nibble a cell: the still modes' brightness step (0..3), SPARKLE's heat (0..15) */
static uint8_t rowv[WF_COLS];                            /* SPECTROGRAM: the steps of the row that is entering */
static uint8_t cnx[WF_COLS], rny[WF_ROWS];               /* ZONES: per column and per row, how far from its region's centre (0..255): the per-cell arithmetic is then a few adds */
#define WF_ACT 192u
static uint16_t act[WF_ACT], band_avg[MTR_BANDS];        /* SPARKLE: the cells that are lit (grid index; their heat is in zone[]); each band's running average of its level (x16) */
static uint32_t nact, fade_acc;
static uint8_t zq[MTR_BANDS];                            /* ZONES: the level (in sixteenths) each region was last drawn for (255: draw it again) */
static mgrid_t wfg;                                      /* the grid: its geometry (centred in the box, whole cells) and its four steps */
#define ox (wfg.g.x0)
#define oy (wfg.g.y0)
#define zget(i) cg_get(zone, (i))
#define zset(i, v) cg_set(zone, (i), (v))
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
static uint32_t cell_step(uint32_t lv, uint32_t thr)     /* 0 off (the dim cell), 1..3 brighter the further the level is above the cell's threshold (thirds of the way to the top, no division) */
{
    if (lv <= thr) return 0u;
    const uint32_t d3 = (lv - thr) * 3u, r = 256u - thr;
    return d3 >= 2u * r ? 3u : d3 >= r ? 2u : 1u;
}
static uint32_t col_level(const uint8_t *spec, uint32_t c, uint32_t cols)      /* the 16 bands spread over the columns, interpolated */
{
    if (cols < 2u) return spec[0];
    const uint32_t pos = c * 15u * 16u / (cols - 1u), b = pos >> 4, f = pos & 15u, b2 = b < 15u ? b + 1u : 15u;
    return ((uint32_t)spec[b] * (16u - f) + (uint32_t)spec[b2] * f) >> 4;
}
static void zones_prepare(uint32_t cols, uint32_t rows)    /* once per geometry: the distance of every column and row from the centre of its 4 x 4 region (0..255) and the region */
{
    for (uint32_t c = 0; c < cols; c++) {
        const uint32_t rc = c * 4u / cols, c0 = rc * cols / 4u, c1 = (rc + 1u) * cols / 4u, sx = c1 - c0 > 1u ? c1 - c0 - 1u : 1u;
        const int32_t dx = (int32_t)(2u * c) - (int32_t)(c0 + c1 - 1u);
        cnx[c] = (uint8_t)((uint32_t)(dx < 0 ? -dx : dx) * 255u / sx > 255u ? 255u : (uint32_t)(dx < 0 ? -dx : dx) * 255u / sx);
    }
    for (uint32_t r = 0; r < rows; r++) {
        const uint32_t rr = r * 4u / rows, r0 = rr * rows / 4u, r1 = (rr + 1u) * rows / 4u, sy = r1 - r0 > 1u ? r1 - r0 - 1u : 1u;
        const int32_t dy = (int32_t)(2u * r) - (int32_t)(r0 + r1 - 1u);
        rny[r] = (uint8_t)((uint32_t)(dy < 0 ? -dy : dy) * 255u / sy > 255u ? 255u : (uint32_t)(dy < 0 ? -dy : dy) * 255u / sy);
    }
}
static uint32_t region_thr(uint32_t c, uint32_t r)       /* ZONES: the threshold grows from the centre of the cell's 4 x 4 region outwards (an octagon, plus a little dither): 0..235 */
{
    const uint32_t nx = cnx[c], ny = rny[r], hi = nx > ny ? nx : ny, lo = nx > ny ? ny : nx, d = (3u * hi + lo) / 4u;
    return d * 190u / 255u + bayer(c, r) * 3u;
}
#define WF_PLANE_Y 722u                                   /* the entering row's plane in the stash (gfx.h map): pitch rows of palette indices, 0..3 the steps, 4 the background */
static void plane_build(uint32_t cols, uint32_t pitch, uint32_t gw)
{
    gfx_rect_stash(0u, WF_PLANE_Y, gw, pitch, 4u);
    for (uint32_t c = 0; c < cols; c++) gfx_rect_stash(c * pitch, WF_PLANE_Y, cfg.cell, cfg.cell, rowv[c]);
}
static void plane_blit(uint32_t gw, uint32_t sy, uint32_t dy, uint32_t h)      /* plane rows sy.. to the grid's rows dy.. (the engine's copy takes at most 127 columns) */
{
    for (uint32_t xo = 0; xo < gw; xo += 127u) gfx_cblit(xo, WF_PLANE_Y + sy, ox + xo, oy + dy, gw - xo < 127u ? gw - xo : 127u, h);
}
#define WF_ONSET 20u                                             /* how far above its running average a band must jump (levels of 256) to count as an onset */
#define WF_MAX_NEW 24u                                           /* the most new flashes a picture may start (a loud passage must not cost more than a quiet one) */
static uint32_t heat_step(uint32_t h) { return h >= 11u ? 3u : h >= 6u ? 2u : h >= 2u ? 1u : 0u; }      /* a flash's brightness step from its heat (0..15) */
static void cell_at(uint32_t c, uint32_t r, uint32_t pitch, uint16_t col) { (void)pitch; mgrid_cell(&wfg, c, r, col); }
static void wf_tick(const mtr_in_t *in)
{
    if (!cg_fit(&wfg.g, in->x, in->y, in->w, in->h, cfg.cell, WF_COLS, WF_ROWS)) return;
    const uint32_t pitch = wfg.g.pitch, cols = wfg.g.cols, rows = wfg.g.rows, gw = cg_w(&wfg.g);
    const uint16_t bg = in->bg;
    mgrid_palette(&wfg, bg, UI_ACCENT);
    const uint16_t *pal = wfg.pal;
    uint8_t spec[MTR_BANDS];
    for (uint32_t b = 0; b < MTR_BANDS; b++) { const uint32_t v = in->paused ? 0u : (uint32_t)in->spec[b] * cfg.gain / 100u; spec[b] = (uint8_t)(v > 255u ? 255u : v); }
    const uint32_t sig = cg_sig(in->w, in->h, pitch, cfg.mode);
    if (in->force || sig != geom) {                                     /* a new context or geometry: the whole grid, dim, then start again */
        geom = sig; acc_q8 = 0; expo = 0; nact = 0; fade_acc = 0;
        mgrid_clear(&wfg, zone, sizeof zone, in->x, in->y, in->w, in->h, bg);
        for (uint32_t b = 0; b < MTR_BANDS; b++) zq[b] = 255u;
        for (uint32_t c = 0; c < cols; c++) rowv[c] = 0;
        plane_build(cols, pitch, gw);
        if (cfg.mode == 1u) zones_prepare(cols, rows);
    }
    if (cfg.mode == 0u) {                                               /* SPECTROGRAM */
        acc_q8 += (uint32_t)in->dt_ms * cfg.speed * 256u / 1000u;
        uint32_t d = acc_q8 >> 8; acc_q8 &= 255u;
        if (!d) return;
        if (d > pitch) d = pitch;                                       /* behind: the picture jumps one cell, never more */
        const uint16_t clut[5] = { pal[0], pal[1], pal[2], pal[3], bg };
        gfx_clut_load(0u, clut, 5u);
        if (rows * pitch > d) gfx_scroll_down(ox, oy, gw, rows * pitch, d);
        expo += d;
        if (expo >= pitch) {                                            /* the entering row is complete: show it whole (the plane holds it), build the next one */
            const uint32_t over = expo - pitch;
            plane_blit(gw, 0u, over, pitch);
            expo = over; step_n++;
            for (uint32_t c = 0; c < cols; c++) { const int32_t lv = (int32_t)col_level(spec, c, cols) + ((int32_t)bayer(c, step_n) - 8) * 2; rowv[c] = (uint8_t)(lv < 40 ? 0u : lv < 110 ? 1u : lv < 180 ? 2u : 3u); }
            plane_build(cols, pitch, gw);
        }
        if (expo) plane_blit(gw, pitch - expo, 0u, expo);               /* the part of the entering row that is in view: the plane's lower rows come in first (the gap, then the cells) */
    } else if (cfg.mode == 3u) {                                        /* SPARKLE */
        const uint32_t div = mtr_degrade_div(in->degrade), period = cfg.fade < 150u ? 10u : cfg.fade / 15u;
        fade_acc += in->dt_ms; uint32_t dec = 0; while (fade_acc >= period && dec < 15u) { fade_acc -= period; dec++; }       /* one heat step every fade / 15 ms */
        if (dec) for (uint32_t k = 0; k < nact; ) {                     /* fade what is lit: a cell is drawn only when its step changes */
            const uint32_t i = act[k], h = zget(i), nh = h > dec ? h - dec : 0u, os = heat_step(h), ns = heat_step(nh);
            zset(i, nh);
            if (os != ns) cell_at(i % WF_COLS, i / WF_COLS, pitch, pal[ns]);
            if (nh) k++; else act[k] = act[--nact];
        }
        step_n++; uint32_t fresh = 0;
        for (uint32_t b = 0; b < MTR_BANDS; b++) {
            const uint32_t lv = (uint32_t)spec[b] << 4, av = band_avg[b];
            band_avg[b] = (uint16_t)(lv > av ? av + ((lv - av) >> 2) : av - ((av - lv) >> 4));        /* quick to follow a rise, slow to forget */
            if (lv < av + (WF_ONSET << 4) || fresh >= WF_MAX_NEW) continue;                          /* not a jump (or the picture's share of flashes is used up) */
            band_avg[b] = (uint16_t)lv;                                 /* the average jumps up with it: a held note is one onset, only a further jump is another */
            const uint32_t s = (lv - av) >> 4, radius = 1u + s / 40u, n = (3u + s / 12u) / div, centre = b * (cols - 1u) / (MTR_BANDS - 1u);
            for (uint32_t k = 0; k < n; k++) {
                const uint32_t h = hx(step_n * 31u + b, k + 5u), dx = (h >> 8) % (2u * radius + 1u), c = centre + dx < radius ? 0u : centre + dx - radius, r = (h >> 16) % rows;
                const uint32_t cc = c < cols ? c : cols - 1u, i = r * WF_COLS + cc;
                if (zget(i)) {                                          /* already lit: refresh it (brighter again if it had faded) */
                    if (heat_step(zget(i)) != 3u) cell_at(cc, r, pitch, pal[3]);
                    zset(i, 15u); continue;
                }
                if (nact >= WF_ACT) continue;
                zset(i, 15u); act[nact++] = (uint16_t)i; fresh++;
                cell_at(cc, r, pitch, pal[3]);
            }
        }
    } else if (cfg.mode == 1u) {                                        /* ZONES: only the regions whose band moved are visited (a band's level in sixteenths: a cell flips when its band crosses its threshold, a sixteenth of the range is finer than the eye) */
        const uint32_t div = mtr_degrade_div(in->degrade);
        for (uint32_t rr = 0; rr < 4u; rr++) for (uint32_t rc = 0; rc < 4u; rc++) {
            const uint32_t band = rr * 4u + rc, q = (uint32_t)spec[band] >> 4;
            if (q == zq[band] && !in->force) continue;
            zq[band] = (uint8_t)q;
            const uint32_t r0 = rr * rows / 4u, r1 = (rr + 1u) * rows / 4u, c0 = rc * cols / 4u, c1 = (rc + 1u) * cols / 4u;
            for (uint32_t r = r0; r < r1; r++) {
                if (div > 1u && (r % div) != (in->frame % div)) { zq[band] = 255u; continue; }           /* a row skipped by the degrade: this band is visited again next picture */
                for (uint32_t c = c0; c < c1; c++) {
                    const uint32_t v = cell_step(q ? (q << 4) | 8u : 0u, region_thr(c, r)), i = r * WF_COLS + c;
                    mgrid_put(&wfg, zone, WF_COLS, c, r, v); (void)i;
                }
            }
        }
    } else {                                                            /* DUST: the still grid, the bands scattered at random over the cells, only the cells that changed are redrawn */
        const uint32_t div = mtr_degrade_div(in->degrade);
        for (uint32_t r = 0; r < rows; r++) {
            if (div > 1u && (r % div) != (in->frame % div)) continue;
            for (uint32_t c = 0; c < cols; c++) {
                const uint32_t h = hx(c + 1u, r + 77u), v = cell_step(spec[(h >> 8) & 15u], h >> 24), i = r * WF_COLS + c;
                mgrid_put(&wfg, zone, WF_COLS, c, r, v); (void)i;
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
