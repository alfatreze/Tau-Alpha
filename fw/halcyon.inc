/* fw/halcyon.inc -- the Halcyon engine selector (B-640, Diagnostic Build first). Everything here is cold code and cold data: the tables (halcyon_tab.h, about 24 KB) live in PSRAM,
 * and the one hot cost is the boot probe in player.c (`hw_hal`). The portable loader is fw/halcyon_hw.h (host-tested, sim/test_halcyon_hw.py); this file only picks a built-in preset
 * and applies it. Diagnostics > HALCYON cycles OFF and the eight built-in presets for the hardware A/B against the old EQ; never persisted (a test switch, like the other
 * Diagnostics toggles). On a bitstream without the engine the row reads NO UNIT and nothing is written. */
#if TAU_HALCYON_FW
#define HAL_TAB COLD_DATA
#include "halcyon_hw.h"

static uint8_t hal_sel;                 /* 0 = engine off (the old EQ drives the output), 1..HAL_NPRESET = built-in preset index + 1 */

COLD_FN static void hal_apply_sel(void)
{
    if (!hw_hal) return;
    if (hal_sel == 0u) { hal_hw_off(); return; }
    hal_ctl_t c;
    const int8_t *p = hal_preset_ctl[hal_sel - 1u];
    c.warmth = p[0]; c.bass = p[1]; c.vocal = p[2]; c.punch = p[3]; c.sibilance = p[4]; c.air = p[5];
    hal_hw_apply_ctl(&c);
}

/* A on the row: the next preset (after the last, back to OFF), inside a gain dip so the change of filter and preamp is not a step in the listener's ears (B-620). */
COLD_FN static void hal_cycle(void)
{
    if (!hw_hal) return;
    gain_dip_begin();
    hal_sel = (uint8_t)((hal_sel + 1u) % (HAL_NPRESET + 1u));
    hal_apply_sel();
    gain_dip_end();
}

COLD_FN static const char *hal_row_text(void)
{
    if (!hw_hal) return "NO UNIT";
    if (hal_sel == 0u) return "OFF";
    return hal_preset_name[hal_sel - 1u];
}
#endif
