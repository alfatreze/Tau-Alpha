#pragma GCC optimize ("Os")
#include "meter.h"
#include "theme.h"
#include "../gfx/gfx.h"
#include "../generated/meter_presets.h"
#include "../cores/ramp_core.h"
#include "../cores/meter_core.h"

/* Waterfall (meters/waterfall_grid/meter.json): a grid of small cells lit by the spectrum. Each cell has a fixed random threshold; a cell is lit when its band's level is above it, brighter the further above (three
 * steps), so the lit fraction follows the level and the picture reads as a heat map. SPECTROGRAM: frequency across, new rows enter at the top and the grid scrolls down (the engine's copy does the scroll,
 * gfx_scroll_down, a few commands a step). ZONES: the grid keeps still; every cell is owned by one band, scattered over the box, and shows that band's level (only cells that changed are redrawn, a quarter of
 * the rows each tick). Cadence and compose are the host's (manifest p2: composed and drawn once per `speed` ms in SPECTROGRAM only, so the scroll happens in the idle buffer and is never seen tearing). The standard degrade: the host skips windows by mtr_in_t.degrade; ZONES also refreshes fewer rows (mtr_degrade_div).
 *
 * A pack-only meter (the framework test, docs/features/meters/METER_FRAMEWORK_FINDINGS.md): the real code exists only with P2_PACK (meter_pack_waterfall_grid.c); the firmware carries a stub that tells the
 * viewer the pack is missing, so a build without the pack costs a few dozen bytes, not the meter. */
#define WF_COLS 48u
#define WF_ROWS 40u
typedef struct { uint8_t mode, gain, cell; uint16_t speed; } wf_cfg_t;
static wf_cfg_t cfg = { 0u, 100u, 8u, 100u };
/* wf_load: the live parameters (the manifest's order) into the working configuration; a pack calls it each tick with the host's table. */
static void wf_load(const uint16_t *v) { cfg.mode = (uint8_t)v[MPP_WF_MODE]; cfg.speed = v[MPP_WF_SPEED]; cfg.gain = (uint8_t)v[MPP_WF_GAIN]; cfg.cell = (uint8_t)v[MPP_WF_CELL]; }

#ifdef P2_PACK
static uint8_t zone[WF_ROWS * WF_COLS];                  /* ZONES: the brightness step (0..3) drawn in each cell */
static uint32_t step_n, geom;                            /* rows entered so far (the noise changes with it); the geometry the grid was drawn for */
static uint32_t hx(uint32_t a, uint32_t b)
{
    uint32_t x = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA6Bu; x ^= x >> 15; x *= 0x2C1B3C6Du; x ^= x >> 12; return x;
}
static uint32_t cell_step(uint32_t lv, uint32_t thr)     /* 0 off, 1..3 brighter the further the level is above the cell's threshold */
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
static void wf_tick(const mtr_in_t *in)
{
    const uint32_t pitch = cfg.cell + 1u;
    uint32_t cols = in->w / pitch, rows = in->h / pitch;
    if (cols > WF_COLS) cols = WF_COLS;
    if (rows > WF_ROWS) rows = WF_ROWS;
    if (!cols || !rows) return;
    const uint16_t bg = in->bg, pal[4] = { bg, rc_ramp(bg, UI_ACCENT, 1u, 3u), rc_ramp(bg, UI_ACCENT, 2u, 3u), UI_ACCENT };
    const uint32_t sig = (in->w << 20) ^ (in->h << 10) ^ (pitch << 4) ^ cfg.mode;
    if (in->force || sig != geom) {                                     /* a new context or geometry: wipe and start again */
        geom = sig; gfx_rect(in->x, in->y, in->w, in->h, bg);
        for (uint32_t i = 0; i < WF_ROWS * WF_COLS; i++) zone[i] = 0;
    }
    uint8_t spec[MTR_BANDS];
    for (uint32_t b = 0; b < MTR_BANDS; b++) { const uint32_t v = in->paused ? 0u : (uint32_t)in->spec[b] * cfg.gain / 100u; spec[b] = (uint8_t)(v > 255u ? 255u : v); }
    if (cfg.mode == 0u) {                                               /* SPECTROGRAM */
        uint32_t steps = in->dt_ms / (cfg.speed ? cfg.speed : 1u);        /* the host draws this meter once per `speed` ms (manifest cadence) and passes the whole periods that elapsed */
        if (!steps) return;
        if (steps > 3u) steps = 3u;
        if (rows > steps) gfx_scroll_down(in->x, in->y, cols * pitch, rows * pitch, steps * pitch);
        for (uint32_t k = 0; k < steps; k++) {
            const uint32_t y = in->y + k * pitch;
            gfx_rect(in->x, y, cols * pitch, pitch, bg);                  /* the row's cells replace what the scroll left there */
            step_n++;
            for (uint32_t c = 0; c < cols; c++) {
                const uint32_t v = cell_step(col_level(spec, c, cols), hx(c, step_n) >> 24);
                if (v) gfx_rect(in->x + c * pitch, y, cfg.cell, cfg.cell, pal[v]);
            }
        }
    } else {                                                            /* ZONES */
        const uint32_t div = mtr_degrade_div(in->degrade), phase = (in->frame / div) & 3u;     /* a quarter of the rows a tick; fewer, less often, when the audio's lead is short */
        for (uint32_t r = phase; r < rows; r += 4u) {
            if ((r / 4u) % div) continue;
            for (uint32_t c = 0; c < cols; c++) {
                const uint32_t h = hx(c + 1u, r + 77u), band = (h >> 8) & 15u, v = cell_step(spec[band], h >> 24);
                if (v != zone[r * WF_COLS + c] || in->force) { zone[r * WF_COLS + c] = (uint8_t)v; gfx_rect(in->x + c * pitch, in->y + r * pitch, cfg.cell, cfg.cell, pal[v]); }
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
