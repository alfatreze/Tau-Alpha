/* SOUND MAP (meters/sound_map/meter.json): a PACK-ONLY meter that shows what the sound IS, not only how loud each band is. It reads the audio statistics (mtr_in_t ABI 1.1, fw2/cores/audio_stats_core.h):
 *   STEREO FIELD  a sound field made of cells: the left channel's level swells a blob from the centre to the left, the right channel's to the right, what the channels share (correlation > 0)
 *                 glows in the middle; the two top rows are a width gauge (the marker sits at the correlation: far right = mono, centre = wide, far left, in red = out of phase).
 *   CHARACTER     five descriptors, a bar of cells each: LOUD (rms), BRIGHT (spectral centroid), NOISE (flat spectrum against a tonal one), CREST (peak over rms) and HITS (how many onsets lately).
 *   LONG MEMORY   one column per SPAN seconds (loudness is the height, brightness the colour from the centroid): a track overview that scrolls left.
 * Without the hardware statistics block (stats_ok = 0) the field and the loudness use the peaks (the field shows no gauge, CREST stays empty); the rest is firmware-only.
 * The host composes nothing for it, degrades and budgets it (p2.cmds, p2.max_cycles). Only the changed cells are drawn. Helpers: meter_grid.h (cells), histring_core.h (the memory rings). */
#pragma GCC optimize ("Os")
#include "meter.h"
#include "theme.h"
#include "../gfx/gfx.h"
#include "../generated/meter_presets.h"
#include "../cores/ramp_core.h"
#include "../cores/audio_stats_core.h"
#include "meter_grid.h"
#include "../cores/histring_core.h"
#define SMP_COLS 66u                                        /* the most cells the fullscreen box holds at the smallest cell */
#define SMP_ROWS 53u
#define SMP_HIST 128u                                       /* a memory ring: samples (a multiple of 4) */
#define SMP_LABEL_W 50u                                     /* the character map's label column */
#define SMP_STEP_RED 4u                                     /* a cell state beyond the palette: the out-of-phase marker */
typedef struct { uint8_t mode, gain, cell, span; } smp_cfg_t;
static smp_cfg_t cfg = { 0u, 100u, 8u, 1u };
static void smp_load(const uint16_t *v) { cfg.mode = (uint8_t)v[MPP_SMP_MODE]; cfg.gain = (uint8_t)v[MPP_SMP_GAIN]; cfg.cell = (uint8_t)v[MPP_SMP_CELL]; cfg.span = (uint8_t)v[MPP_SMP_SPAN]; }
#ifdef P2_PACK
static uint8_t state[CG_BYTES(SMP_COLS, SMP_ROWS)];       /* the cell grid: a nibble a cell, the step drawn */
static mgrid_t grid;
static uint32_t ring_l[HR_WORDS(SMP_HIST)], ring_c[HR_WORDS(SMP_HIST)], head, geom;
static uint32_t acc_ms, acc_loud, acc_cn, acc_cd, acc_n;   /* the second being collected (memory) */
static uint8_t sm_l, sm_r, sm[5];                            /* smoothed channel levels; smoothed descriptors */
static uint32_t drawn_mem;                                   /* the newest memory column as drawn (loudness << 8 | centroid), 0xFFFFFFFF = nothing */

static uint32_t lvl_of(uint32_t x, uint32_t gain)            /* 0..24575 (the scale of the peaks) -> 0..255, square-root curve so quiet music shows, then the gain */
{
    uint32_t v = as_isqrt32(x * 2646u / 1000u) * gain / 100u;
    return v > 255u ? 255u : v;
}
static uint32_t ease(uint32_t old, uint32_t now)             /* up fast, down slowly */
{
    if (now >= old) return old + (now - old + 1u) / 2u;
    return old - (old - now + 7u) / 8u;
}
static void put_field(uint32_t c, uint32_t r, uint32_t step) { mgrid_put(&grid, state, SMP_COLS, c, r, step); }
static void put_gauge(uint32_t c, uint32_t r, uint32_t step)
{
    const uint32_t i = r * SMP_COLS + c;
    if (cg_get(state, i) == step) return;
    cg_set(state, i, step); mgrid_cell(&grid, c, r, step == SMP_STEP_RED ? UI_RED : grid.pal[step & 3u]);
}
static void stereo(const mtr_in_t *in, uint32_t cols, uint32_t rows)
{
    const uint32_t ok = in->stats_ok;
    const uint32_t l = in->paused ? 0u : lvl_of(ok ? in->rms_l : in->peak_l, cfg.gain), r = in->paused ? 0u : lvl_of(ok ? in->rms_r : in->peak_r, cfg.gain);
    sm_l = (uint8_t)ease(sm_l, l); sm_r = (uint8_t)ease(sm_r, r);
    const int32_t corr = (ok && !in->paused) ? in->corr : 0;
    const uint32_t shared = corr > 0 ? (uint32_t)(sm_l < sm_r ? sm_l : sm_r) * (uint32_t)corr / 256u : 0u;
    const uint32_t g0 = rows > 6u ? 2u : 0u, fr = rows - g0, mid = g0 + fr / 2u;                    /* the gauge rows, then the field */
    for (uint32_t c = 0; c < cols; c++) {
        const uint32_t two = 2u * c, centre = cols - 1u;
        const uint32_t dist = (two > centre ? two - centre : centre - two) * 256u / (cols > 1u ? cols : 1u);
        const uint32_t side = two < centre ? sm_l : two > centre ? sm_r : ((uint32_t)sm_l + sm_r) / 2u;
        for (uint32_t row = g0; row < rows; row++) {
            const uint32_t vd = (row > mid ? row - mid : mid - row) * 510u / fr;                       /* a diamond that reaches the box's edges at full level in BOTH directions (a small box is not a flat sliver) */
            const uint32_t e = side > dist + vd ? side - dist - vd : 0u;
            uint32_t step = e > 60u ? 3u : e > 28u ? 2u : e ? 1u : 0u;
            if (dist + vd < shared) step = 3u;                                             /* the shared core, the same shape */
            put_field(c, row, step);
        }
    }
    if (g0) {                                                                                       /* the width gauge */
        const uint32_t mx = ok ? (uint32_t)((corr + 256) * (int32_t)(cols - 1u) / 512) : cols;      /* cols = no marker */
        for (uint32_t c = 0; c < cols; c++) {
            const uint32_t d = c > mx ? c - mx : mx - c, centre = (2u * c == cols - 1u) || (2u * c == cols) || (2u * c + 2u == cols);
            const uint32_t step = (mx < cols && d == 0u) ? (corr < 0 ? SMP_STEP_RED : 3u) : (mx < cols && d == 1u) ? 2u : centre ? 1u : 0u;
            put_gauge(c, 0u, step); put_gauge(c, 1u, step);
        }
    }
}
static void character(const mtr_in_t *in, uint32_t cols, uint32_t rows)
{
    uint32_t sum = 0, mx = 0;
    for (uint32_t b = 0; b < MTR_BANDS; b++) { sum += in->spec[b]; if (in->spec[b] > mx) mx = in->spec[b]; }
    const uint32_t p = in->paused;
    uint32_t t[5];
    t[0] = p ? 0u : lvl_of(in->stats_ok ? (uint32_t)(in->rms_l + in->rms_r) / 2u : (in->peak_l + in->peak_r) / 2u, cfg.gain);
    t[1] = p ? 0u : in->centroid;
    t[2] = (p || mx < 8u) ? 0u : (sum / MTR_BANDS) * 255u / mx;
    t[3] = (p || !in->stats_ok || in->crest < 16u) ? 0u : (uint32_t)(in->crest - 16u) * 255u / 48u > 255u ? 255u : (uint32_t)(in->crest - 16u) * 255u / 48u;
    static uint32_t hits;
    hits = hits - hits / 16u + (in->onset && !p ? 48u : 0u); if (hits > 255u) hits = 255u;
    t[4] = hits;
    const uint32_t gh = rows / 5u, off = (rows - 5u * gh) / 2u;                                       /* the groups centred in the box */
    for (uint32_t d = 0; d < 5u; d++) {
        sm[d] = (uint8_t)ease(sm[d], t[d]);
        const uint32_t n = (uint32_t)sm[d] * cols / 256u, frac = (uint32_t)sm[d] * cols % 256u;
        for (uint32_t k = 0; k + 1u < gh || gh == 1u; k++) {                                         /* the group's rows (its last row is the gap) */
            const uint32_t row = off + d * gh + k;
            if (row >= rows) break;
            for (uint32_t c = 0; c < cols; c++) put_field(c, row, c < n ? 3u : (c == n && frac > 128u) ? 2u : (c == n && frac > 48u) ? 1u : 0u);
            if (gh == 1u) break;
        }
    }
}
static void memory(const mtr_in_t *in, uint32_t cols, uint32_t force)
{
    const uint32_t loud = in->paused ? 0u : lvl_of(in->stats_ok ? (uint32_t)(in->rms_l + in->rms_r) / 2u : (in->peak_l + in->peak_r) / 2u, cfg.gain);
    uint32_t pushed = 0;
    if (!in->paused) {
        acc_ms += in->dt_ms; acc_loud += loud; acc_n++; acc_cn += (uint32_t)in->centroid * loud; acc_cd += loud;
        if (acc_ms >= (uint32_t)cfg.span * 1000u) {
            head = hr_back(head, SMP_HIST); hr_store(ring_l, head, acc_n ? acc_loud / acc_n : 0u); hr_store(ring_c, head, acc_cd ? acc_cn / acc_cd : 0u);
            acc_ms = 0; acc_loud = 0; acc_n = 0; acc_cn = 0; acc_cd = 0; pushed = 1;
        }
    }
    const uint32_t h = grid.g.rows * grid.g.pitch, w = grid.g.pitch - 1u, live_c = acc_cd ? acc_cn / acc_cd : in->centroid, live = (uint32_t)(loud << 8) | (live_c & 255u);
    const uint16_t lo = rc_ramp(in->bg, UI_ACCENT, 3u, 8u);
    if (pushed || force) {                                                                         /* every column again (the memory scrolled one) */
        uint8_t l[SMP_COLS], cc[SMP_COLS];
        hr_unwrap(ring_l, head, SMP_HIST, l, cols); hr_unwrap(ring_c, head, SMP_HIST, cc, cols);
        for (uint32_t c = 0; c + 1u < cols; c++) {
            const uint32_t age = cols - 2u - c;                                                    /* the newest stored column is just left of the live one */
            gfx_bar(cg_x(&grid.g, c), grid.g.y0, w, h, (uint32_t)l[age] * h / 256u, rc_ramp(lo, UI_ACCENT, cc[age], 255u), in->bg);
        }
        drawn_mem = 0xFFFFFFFFu;
    }
    if (live != drawn_mem) {                                                                       /* the live column, redrawn when it changed */
        drawn_mem = live;
        gfx_bar(cg_x(&grid.g, cols - 1u), grid.g.y0, w, h, loud * h / 256u, rc_ramp(lo, UI_ACCENT, live_c > 255u ? 255u : live_c, 255u), in->bg);
    }
}
static void smp_tick(const mtr_in_t *in)
{
    const uint32_t lab = cfg.mode == 1u ? SMP_LABEL_W : 0u;
    if (!cg_fit(&grid.g, in->x + lab, in->y, in->w - lab, in->h, cfg.cell, SMP_COLS, SMP_ROWS)) return;
    const uint32_t cols = grid.g.cols, rows = grid.g.rows, sig = cg_sig(in->w, in->h, grid.g.pitch, cfg.mode);
    mgrid_palette(&grid, in->bg, UI_ACCENT);
    uint32_t fresh = 0;
    if (in->force || sig != geom) {
        geom = sig; fresh = 1u;
        if (cfg.mode == 2u) { gfx_rect(in->x, in->y, in->w, in->h, in->bg); cg_zero(state, sizeof state); hr_clear(ring_l, SMP_HIST); hr_clear(ring_c, SMP_HIST); head = 0; acc_ms = acc_loud = acc_n = acc_cn = acc_cd = 0; }
        else mgrid_clear(&grid, state, sizeof state, in->x, in->y, in->w, in->h, in->bg);
        sm_l = sm_r = 0; for (uint32_t d = 0; d < 5u; d++) sm[d] = 0;
        if (cfg.mode == 1u) {
            static const char *const name[5] = { "LOUD", "BRIGHT", "NOISE", "CREST", "HITS" };
            const uint32_t gh = rows / 5u, off = (rows - 5u * gh) / 2u;
            for (uint32_t d = 0; d < 5u; d++) {
                gfx_text(in->x, cg_y(&grid.g, off + d * gh) , name[d], UI_DIM, in->bg);
                if (gh > 1u) gfx_rect(grid.g.x0, cg_y(&grid.g, off + d * gh + gh - 1u), cols * grid.g.pitch, grid.g.pitch, in->bg);            /* the gap row between groups */
            }
        }
    }
    if (cfg.mode == 0u) stereo(in, cols, rows);
    else if (cfg.mode == 1u) character(in, cols, rows);
    else memory(in, cols, fresh);
}
#endif
