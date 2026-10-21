#pragma GCC optimize ("Os")
#include "meter.h"
#include "theme.h"
#include "../gfx/gfx.h"
#include "../generated/meter_presets.h"
#include "../cores/meter_core.h"             /* the shared ballistics, peak cap, band mapping and redraw cache (host-tested against the original arithmetic) */

/* Winamp bars (meters/winamp_bars/meter.json): solid per-band bars with an eased level and a falling peak cap. Defaults = the FLUID preset. */
#define BANDS_MIN 4u
#define BANDS_MAX 16u
typedef struct { uint8_t bands, ease, attack, release, peak_on, peak_gravity, peak_fall; uint16_t peak_hold_ms; } wb_cfg_t;
static wb_cfg_t cfg = { 16u, 2u, 55u, 22u, 1u, 1u, 35u, 200u };
/* wb_load: the live parameters (in the manifest's order) into the working configuration. The firmware calls it from apply(); a pack (P2_PACK, fw2/ui/meter_pack_winamp_bars.c) calls it with the host's table each tick. */
static void wb_load(const uint16_t *v)
{
    cfg.bands = (uint8_t)v[MPP_BARS_BANDS]; cfg.ease = (uint8_t)v[MPP_BARS_EASE]; cfg.attack = (uint8_t)v[MPP_BARS_ATTACK]; cfg.release = (uint8_t)v[MPP_BARS_RELEASE];
    cfg.peak_on = (uint8_t)v[MPP_BARS_PEAK_ON]; cfg.peak_gravity = (uint8_t)v[MPP_BARS_PEAK_GRAVITY]; cfg.peak_hold_ms = v[MPP_BARS_PEAK_HOLD_MS]; cfg.peak_fall = (uint8_t)v[MPP_BARS_PEAK_FALL];
}
#ifndef P2_PACK
const char *meter_winamp_bars_preset_name(uint32_t i) { return mpp_bars_name[i < MPP_BARS_PRESETS ? i : 0u]; }
uint16_t meter_winamp_bars_mv[MPP_BARS_N];                  /* the live parameters, in the manifest's order (the Configure page edits these) */
void meter_winamp_bars_apply(void) { wb_load(meter_winamp_bars_mv); }
void meter_winamp_bars_preset(uint32_t i)
{
    const uint16_t *v = mpp_bars[i < MPP_BARS_PRESETS ? i : 0u];
    for (uint32_t k = 0; k < MPP_BARS_N; k++) meter_winamp_bars_mv[k] = v[k];
    meter_winamp_bars_apply();
}
#endif

static uint8_t disp[BANDS_MAX], drawn[BANDS_MAX], peak_drawn[BANDS_MAX]; static int16_t vel[BANDS_MAX]; static mtr_peak_t pk[BANDS_MAX];

void meter_winamp_bars_tick(const mtr_in_t *in)
{
    const uint32_t x0 = in->x, y = in->y, w = in->w, h = in->h, bands = cfg.bands < BANDS_MIN ? BANDS_MIN : cfg.bands > BANDS_MAX ? BANDS_MAX : cfg.bands, gap = 2u;
    const uint32_t colw = (w > gap * (bands - 1u)) ? (w - gap * (bands - 1u)) / bands : 1u;
    if (in->force) gfx_rect(x0, y, w, h, in->bg);                      /* the context changed: wipe the box once, then every band repaints */
    const mtr_peak_cfg_t pcfg = { cfg.peak_on, cfg.peak_gravity, cfg.peak_hold_ms, cfg.peak_fall };
    for (uint32_t b = 0; b < bands; b++) {
        uint32_t target = mtr_band_target(in->spec, MTR_BANDS, bands, b);
        if (in->paused) target = 0u;
        const uint32_t rate = (target >= disp[b]) ? cfg.attack : cfg.release;
        disp[b] = mtr_ease(disp[b], (uint8_t)target, cfg.ease, rate, &vel[b]);
        mtr_peak_step(&pk[b], disp[b], &pcfg, in->dt_ms);
        if (!mtr_delta(&drawn[b], &peak_drawn[b], disp[b], pk[b].peak, in->force)) continue;
        const uint32_t x = x0 + b * (colw + gap);
        uint32_t bh = (disp[b] * h) / 255u;
        if (bh < 2u) bh = 2u;
        gfx_bar(x, y, colw, h, bh, UI_ACCENT, in->bg);                  /* the whole column in ONE command: the bottom bh rows the bar, the rows above it (the old bar top and the old cap) erased */
        if (cfg.peak_on) {
            const uint32_t ph = (pk[b].peak * h) / 255u;
            if (ph > bh + 1u && ph < h) gfx_rect(x, y + h - ph, colw, 1u, UI_WHITE);
        }
    }
}
