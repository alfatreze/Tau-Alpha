#pragma GCC optimize ("Os")
#include "meter.h"
#include "theme.h"
#include "../gfx/gfx.h"
#include "../generated/meter_presets.h"
#include "../cores/meter_core.h"
#include "../cores/vu_master_core.h"
#include "../generated/vu_segment_table.h"

/* Master VU (docs/METER_VU_MASTERING_SPEC.md): mastering-style segmented dB peak ladders, L above R, 24 segments each (-48..0 dBFS, 2 dB a segment), green / yellow / red zones (the theme's OK, WARN and
 * DANGER roles), a held-peak marker per channel. The dB-to-segment table and the "at most three coalesced rectangles" rule are the old player's host-tested core. The technical-info overlay of the old meter
 * (format, rate, CPU, decode share, SDRAM busy) is not ported: those readings are on the Info page. */
#define LABEL_W 18u
#define ROW_H 12u
#define SCALE_Y 26u
#define ROW2_Y 28u
#define SEG_GAP 1u
#define PEAK_MARK_H 4u
static mtr_peak_t pk[2]; static uint8_t disp[2], lit_drawn[2] = { 0xFFu, 0xFFu }, peak_drawn[2] = { 0xFFu, 0xFFu }; static int16_t vel[2];
static const mtr_peak_cfg_t pcfg = { 1u, 1u, 200u, 35u };

static uint32_t seg_x(uint32_t lx, uint32_t lw, uint32_t seg, uint32_t *seg_w_out)
{
    const uint32_t span = SEG_GAP * (VUM_SEGMENTS - 1u), seg_w = (lw > span) ? (lw - span) / VUM_SEGMENTS : 1u;
    if (seg_w_out) *seg_w_out = seg_w;
    return lx + seg * (seg_w + SEG_GAP);
}
static uint8_t fixed; static uint16_t fixed_c[3];                      /* a preset with its own zone colours (STANDARD, CUSTOM EXAMPLE) instead of the theme's roles */
const char *meter_vu_master_preset_name(uint32_t i) { return mpp_vu_name[i < MPP_VU_PRESETS ? i : 0u]; }
uint16_t meter_vu_master_mv[MPP_VU_N];
void meter_vu_master_apply(void)
{
    const uint16_t *v = meter_vu_master_mv;
    fixed = (uint8_t)(v[MPP_VU_COLOR_MODE] == 1u); fixed_c[0] = v[MPP_VU_COLOR_GREEN]; fixed_c[1] = v[MPP_VU_COLOR_YELLOW]; fixed_c[2] = v[MPP_VU_COLOR_RED];
}
void meter_vu_master_preset(uint32_t i)
{
    for (uint32_t k = 0; k < MPP_VU_N; k++) meter_vu_master_mv[k] = mpp_vu[i < MPP_VU_PRESETS ? i : 0u][k];
    meter_vu_master_apply();
}
static uint16_t zone_color(uint32_t zone) { if (fixed) return fixed_c[zone > 2u ? 2u : zone]; return zone == 0u ? th_role[TR_OK] : zone == 1u ? th_role[TR_WARN] : th_role[TR_DANGER]; }

static void ladder(uint32_t ch, uint32_t lx, uint32_t ly, uint32_t lw, uint16_t bg, uint32_t peak, uint32_t force, uint32_t dt)
{
    uint32_t idx = peak >> 7; if (idx > 255u) idx = 255u;
    if (force) { disp[ch] = (uint8_t)idx; vel[ch] = 0; }                                      /* snap on open: no ramp-in from a stale index */
    else disp[ch] = mtr_ease(disp[ch], (uint8_t)idx, 2u, idx >= disp[ch] ? 85u : 40u, &vel[ch]);
    const uint32_t lit = vum_peak_to_segments((uint32_t)disp[ch] << 7, vu_db_to_segment, VU_SEGMENT_TABLE_N);
    mtr_peak_step(&pk[ch], (uint8_t)((lit * 255u) / VUM_SEGMENTS), &pcfg, dt);
    uint32_t peak_seg = ((uint32_t)pk[ch].peak * VUM_SEGMENTS) / 255u;
    if (peak_seg > VUM_SEGMENTS) peak_seg = VUM_SEGMENTS;
    if (!force && lit == lit_drawn[ch] && peak_seg == peak_drawn[ch]) return;
    lit_drawn[ch] = (uint8_t)lit; peak_drawn[ch] = (uint8_t)peak_seg;
    gfx_rect(lx, ly, lw, ROW_H, bg);
    uint32_t zone[3], start[3], count[3];
    const uint32_t n = vum_coalesce(lit, zone, start, count);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t seg_w; const uint32_t x0 = seg_x(lx, lw, start[i], &seg_w), x1 = seg_x(lx, lw, start[i] + count[i] - 1u, 0);
        gfx_rect(x0, ly, (x1 + seg_w) - x0, ROW_H, zone_color(zone[i]));
    }
    if (peak_seg > lit && peak_seg > 0u) {
        uint32_t seg_w; const uint32_t px = seg_x(lx, lw, peak_seg - 1u, &seg_w), my = ly + (ROW_H - PEAK_MARK_H) / 2u;
        gfx_rect(px, my, seg_w, PEAK_MARK_H, zone_color(vum_zone_of(peak_seg)));
    }
}
static void face(uint32_t x0, uint32_t y0, uint32_t w, uint16_t bg)
{
    const uint32_t lx = x0 + LABEL_W, lw = (w > LABEL_W) ? w - LABEL_W : 1u;
    gfx_rect(x0, y0, w, ROW_H + ROW2_Y + 4u, bg);
    gfx_text(x0, y0 - 2u, "L", UI_DIM, bg);
    gfx_text(x0, y0 + ROW2_Y - 2u, "R", UI_DIM, bg);
    const uint32_t sy = y0 + SCALE_Y;
    gfx_rect(lx, sy, lw, 1u, UI_FAINT);
    static const int32_t ticks_db[7] = { -40, -30, -20, -12, -6, -3, 0 };
    for (uint32_t t = 0; t < 7u; t++) {
        uint32_t seg = (uint32_t)((ticks_db[t] + 48) / 2); if (seg >= VUM_SEGMENTS) seg = VUM_SEGMENTS - 1u;
        uint32_t seg_w; const uint32_t tx = seg_x(lx, lw, seg, &seg_w);
        gfx_rect(tx, sy - 2u, (seg_w > 1u) ? 1u : seg_w, 5u, UI_FAINT);
    }
}
void meter_vu_master_tick(const mtr_in_t *in)
{
    const uint32_t x0 = in->x, y0 = in->y, w = in->w; const uint16_t bg = in->bg; const uint32_t force = in->force;
    if (force) {
        gfx_rect(x0, y0, w, in->h, bg);
        lit_drawn[0] = lit_drawn[1] = peak_drawn[0] = peak_drawn[1] = 0xFFu;
        face(x0, y0, w, bg);
    }
    const uint32_t lx = x0 + LABEL_W, lw = (w > LABEL_W) ? w - LABEL_W : 1u;
    ladder(0u, lx, y0, lw, bg, in->paused ? 0u : in->peak_l, force, in->dt_ms);
    ladder(1u, lx, y0 + ROW2_Y, lw, bg, in->paused ? 0u : in->peak_r, force, in->dt_ms);
}
