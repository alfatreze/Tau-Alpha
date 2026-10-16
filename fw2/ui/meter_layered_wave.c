#pragma GCC optimize ("Os")
#include "meter.h"
#include "theme.h"
#include "../gfx/gfx.h"
#include "../platform/mmio.h"
#include "../generated/lw_presets.h"

/* Layered Wave (docs/features/meters/LAYERED_WAVE_METER_SPEC.md): nested, mirrored envelope layers on a solid background, driven by the 16 spectrum bands (or, for the DYNAMICS split, the loudest sample).
 * The module body is the old player's, unchanged (fw/layered_wave.inc: integer-only, the C twin of tools/meters/preview/meters/layered_wave.js, compared command for command by
 * sim/test_layered_wave_golden.py); this file supplies what that body expects: plain gfx_rect, the parameters of the selected preset, the colour roles, and a proven PSRAM window for the history ring.
 * The whole object is cold code. Experimental parameters (anti-aliasing, blend modes, per-layer gains) are the Diagnostic Build's in the old player and are not part of Player 2. */
#define COLD_FN3
#define COLD_DATA
#define fb_rect gfx_rect
#define WAVE_COLS 256u
#define fig_rect gfx_rect
#define ui_accent UI_ACCENT
#ifndef P2_HOST
#define MTR_PSRAM __attribute__((section(".psram_state")))
#else
#define MTR_PSRAM
#endif
#define MV_LAYERED_WAVE(n) ((int32_t)lw_mv[MP_LAYERED_WAVE_##n])
#define paused (in->paused)
#define TAU_FX_METER_EXPERIMENTAL 0
#ifndef TAU_DIAGNOSTIC
#define TAU_DIAGNOSTIC 0
#endif
static uint16_t lw_mv[MP_LAYERED_WAVE_N];
static uint8_t wviz_force;
static uint8_t lw_ps;                 /* the PSRAM window: 0 untested, 1 proven, 2 failed */
#ifndef P2_HOST
extern uint32_t __psram_state_start[], __psram_state_end[];
#endif
static int mtr_psram_ready(void)
{
    if (!lw_ps) {
        static const uint32_t pat[2] = { 0xA5C33C5Au, 0x5A3CC3A5u };
        lw_ps = 1u;
        if (REG_RD(0x80000088u) != 0x50535231u || !((REG_RD(0x800000A0u) >> 16) & 1u)) lw_ps = 2u;       /* the PSRAM controller and its CPU window must be in this bitstream */
        else {
            volatile uint32_t *w0 = (volatile uint32_t *)__psram_state_start, *w1 = (volatile uint32_t *)__psram_state_end - 1;
            for (uint32_t k = 0; k < 2u; k++) { *w0 = pat[k]; *w1 = ~pat[k]; if (*w0 != pat[k] || *w1 != ~pat[k]) lw_ps = 2u; }
        }
    }
    return lw_ps == 1u;
}
#include "../../fw/layered_wave.inc"

static uint32_t preset_cur = 0xFFu, want;
uint32_t lw_unavailable(void) { return lw_ps == 2u; }
void meter_layered_wave_preset(uint32_t i) { want = i < LW_PRESET_N ? i : 0u; }
const char *meter_layered_wave_preset_name(uint32_t i) { return lw_preset_name[i < LW_PRESET_N ? i : 0u]; }

void meter_layered_wave_tick(const mtr_in_t *in0)
{
    if (preset_cur != want) { for (uint32_t i = 0; i < MP_LAYERED_WAVE_N; i++) lw_mv[i] = lw_preset[want][i]; preset_cur = want; }
    static int8_t wv[WAVE_COLS];                                   /* the loudest sample of either channel as a flat envelope: the DYNAMICS split only takes the peak */
    const uint32_t pk = (in0->peak_l > in0->peak_r ? in0->peak_l : in0->peak_r) * 127u / 24575u;
    wv[0] = (int8_t)(pk > 127u ? 127u : pk);
    mtr_in_t in = *in0; in.wave = wv;
    wviz_force = in.force;
    lw_tick(&in);
    if (lw_ps == 2u && in.force) gfx_text(in.x + 8u, in.y + in.h / 2u - 8u, "LAYERED WAVE UNAVAILABLE", UI_RED, in.bg);
}
